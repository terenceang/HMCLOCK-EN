/**
 ****************************************************************************************
 *
 * @file user_peripheral.c
 *
 * @brief 外设项目源代码文件，主要实现BLE外设功能、时钟管理、EPD屏幕显示及OTP数据读取等功能
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 *
 ****************************************************************************************
 */

/**
 ****************************************************************************************
 * @addtogroup APP
 * @{
 ****************************************************************************************
 */

/*
 * 包含头文件
 ****************************************************************************************
 */

#include "rwip_config.h"             // 软件配置
#include "gattc_task.h"              // GATT客户端任务相关定义
#include "gap.h"                     // GAP层相关定义
#include "app_easy_timer.h"          // 应用层定时器功能
#include "user_peripheral.h"         // 本文件接口声明
#include "user_custs1_impl.h"        // 自定义服务1实现
#include "user_custs1_def.h"         // 自定义服务1定义
#include "co_bt.h"                   // 蓝牙协议协议相关定义
#include "hw_otpc.h"                 // OTP控制器硬件接口

#include "epd.h"                     // EPD电子纸屏幕驱动

/*
 * 类型定义
 ****************************************************************************************
 */


/*
 * 全局变量定义
 ****************************************************************************************
 */

int app_connection_idx                          __SECTION_ZERO("retention_mem_area0"); // 连接索引，使用 retention 内存区域保存
timer_hnd app_clock_timer_used                  __SECTION_ZERO("retention_mem_area0"); // 时钟定时器句柄，retention内存保存
timer_hnd app_param_update_request_timer_used   __SECTION_ZERO("retention_mem_area0"); // 参数更新请求定时器句柄，retention内存保存

int adv_state = 0;                          // 广播状态：0-未广播，1-正在广播
static int otp_btaddr[2];                      // 从OTP读取的蓝牙地址
static int otp_boot;                           // 从OTP读取的启动相关数据
static char adv_name[20];                      // 广播名称缓冲区
char *bt_id = adv_name+7;                      // 蓝牙ID在广播名称中的起始位置（"DCLK-"之后）
int clock_interval;                            // 时钟更新间隔（秒）
int clock_fixup_value;                         // 时钟修正值
int clock_fixup_count;                         // 时钟修正计数器
static int first_timer_trigger = 0;            // 标志：是否为第一次定时器触发（用于整分钟对齐）
static int first_update_seconds = 0;           // 第一次触发时需要更新的秒数

// EPD版本信息（volatile确保不被优化，用于版本检测）
const volatile u32 epd_version[3] = {0xF9A51379, ~0xF9A51379, EPD_VERSION};

extern int year,month; // 当前时间变量
extern int second; // 当前秒数，用于计算到整分钟的剩余时间

/*
 * 函数定义
 ****************************************************************************************
*/


/**
 ****************************************************************************************
 * @brief 在GAPM_START_ADVERTISE_CMD参数结构的广播或扫描响应数据中添加AD结构
 * @param[in] cmd               GAPM_START_ADVERTISE_CMD参数结构
 * @param[in] ad_struct_data    AD结构数据缓冲区
 * @param[in] ad_struct_len     AD结构长度
 * @param[in] adv_connectable   是否为可连接广播事件，控制广播数据最大长度（可连接时28字节，否则31字节）
 ****************************************************************************************
 */
static void app_add_ad_struct(struct gapm_start_advertise_cmd *cmd, void *ad_struct_data, uint8_t ad_struct_len, uint8_t adv_connectable)
{
    // 根据是否可连接确定广播数据最大长度
    uint8_t adv_data_max_size = (adv_connectable) ? (ADV_DATA_LEN - 3) : (ADV_DATA_LEN);

    // 优先添加到广播数据
    if ((adv_data_max_size - cmd->info.host.adv_data_len) >= ad_struct_len)
    {
        memcpy(&cmd->info.host.adv_data[cmd->info.host.adv_data_len], ad_struct_data, ad_struct_len);
        cmd->info.host.adv_data_len += ad_struct_len;
    }
    // 广播数据空间不足时添加到扫描响应数据
    else if ((SCAN_RSP_DATA_LEN - cmd->info.host.scan_rsp_data_len) >= ad_struct_len)
    {
        memcpy(&cmd->info.host.scan_rsp_data[cmd->info.host.scan_rsp_data_len], ad_struct_data, ad_struct_len);
        cmd->info.host.scan_rsp_data_len += ad_struct_len;
    }
    // 空间不足时触发断言警告
    else
    {
        ASSERT_WARNING(0);
    }
}


/**
 ****************************************************************************************
 * @brief 参数更新请求定时器回调函数
 *        当定时器超时，发起连接参数更新请求
 ****************************************************************************************
*/
static void param_update_request_timer_cb()
{
    app_easy_gap_param_update_start(app_connection_idx);  // 发起参数更新
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;  // 重置定时器句柄
}


/**
 ****************************************************************************************
 * @brief 读取OTP（一次性可编程）存储器中的值
 *        主要读取蓝牙地址和启动信息，并生成广播名称
 ****************************************************************************************
 */
static void read_otp_value(void)
{
	hw_otpc_init();               // 初始化OTP控制器
	hw_otpc_manual_read_on(false); // 关闭手动读取模式
	
	// 从OTP特定地址读取数据
	otp_boot = *(u32*)(0x07f8fe00);    // 读取启动相关数据
	otp_btaddr[0] = *(u32*)(0x07f8ffa8); // 读取蓝牙地址低32位
	otp_btaddr[1] = *(u32*)(0x07f8ffac); // 读取蓝牙地址高32位
	
	hw_otpc_disable();            // 禁用OTP控制器

	// 处理蓝牙地址，生成设备唯一标识
	u32 ba0 = otp_btaddr[0];
	u32 ba1 = otp_btaddr[1];

	ba1 = (ba1<<8)|(ba0>>24);
	ba0 &= 0x00ffffff;
	ba0 ^= ba1;

	// 生成广播名称（格式：DCLK-XXYYZZ，XXYYZZ为蓝牙地址后三段）
	u8 *ba = (u8*)&ba0;
	sprintf(adv_name+2, "DCLK-%02x%02x%02x", ba[2], ba[1], ba[0]);
	int name_len = strlen(adv_name+2);
	
	// 如果设备名称未设置，则使用生成的名称
	if(device_info.dev_name.length==0){
		device_info.dev_name.length = name_len;
		memcpy(device_info.dev_name.name, adv_name+2, name_len);
	}

	// 构造AD结构：第一个字节为长度，第二个字节为AD类型（完整名称）
	adv_name[0] = name_len+1;
	adv_name[1] = GAP_AD_TYPE_COMPLETE_NAME;
}

// 外部声明的区域表基地址（用于内存相关操作）
extern int Region$$Table$$Base;

/**
 ****************************************************************************************
 * @brief 应用初始化函数
 *        初始化OTP数据、定时器、屏幕、蓝牙等模块
 ****************************************************************************************
 */
void user_app_init(void)
{
	read_otp_value();  // 读取OTP数据，初始化广播名称

	printk("\n\nuser_app_init! %s %08x\n", __TIME__, epd_version[2]);
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;  // 初始化参数更新定时器
	app_clock_timer_used = EASY_TIMER_INVALID_TIMER;                 // 初始化时钟定时器

	clock_interval = 60; // 时钟更新间隔设置为60秒
	clock_fixup_value = 0; // 初始化时钟修正值
	clock_fixup_count = 0; // 初始化时钟修正计数器

	first_timer_trigger = 0; // 初始化第一次触发标志

	adv_state = 0; // 初始化为未广播状态
	fspi_config(0x00030605); // 配置FSPI接口

	selflash(otp_boot); // 根据OTP启动数据执行自闪存操作

	// 初始化EPD屏幕（2.13黑白屏，6个测试点）
	epd_hw_init(0x23200700, 0x05210006, detect_w, detect_h, detect_mode | ROTATE_3);
	if(epd_detect()==0){  // 如果检测不到屏幕，尝试另一种配置（5个测试点）
		epd_hw_init(0x23111000, 0x07210120, detect_w, detect_h, detect_mode | ROTATE_3);
		epd_detect();
	}

	app_connection_idx = -1; // 初始化连接索引为无效值
    default_app_on_init();   // 执行默认应用初始化
}


/**
 ****************************************************************************************
 * @brief 时钟快慢修正函数
 * @param[in] diff_sec  自上次对时后的误差秒数（正数表示快了，负数表示慢了）
 * @param[in] minutes   自上次对时后经过的分钟数
 * @note 计算并累积时钟修正值，用于调整定时器间隔，补偿时钟误差
 ****************************************************************************************
 */
void clock_fixup_set(int diff_sec, int minutes)
{
	// 计算新的修正值（基于4096精度的分数计算）
	int new_fixup_value = diff_sec*100*4096/minutes;
	clock_fixup_value += new_fixup_value; // 累积修正值
}


/**
 ****************************************************************************************
 * @brief 应用时钟修正值
 * @return 本次需要调整的毫秒数
 * @note 从累积的修正计数器中提取整数部分作为本次调整值，保留余数
 ****************************************************************************************
 */
static int clock_fixup(void)
{
	int value;

	clock_fixup_count += clock_fixup_value; // 累积修正计数

	value = clock_fixup_count>>12; // 右移12位（除以4096）得到整数部分
	clock_fixup_count &= 0xfff;    // 保留低12位作为余数

	return value; // 返回本次调整的毫秒数
}

extern int adcval;  // ADC电压值变量
/**
 ****************************************************************************************
 * @brief 应用时钟定时器回调函数
 *        定时更新时钟、推送时钟数据、处理屏幕显示，并根据需要重启广播
 ****************************************************************************************
 */
static void app_clock_timer_cb(void)
{
	int adj = clock_fixup(); // 获取时钟修正值
	int update_seconds;
	
	if(first_timer_trigger) {
		first_timer_trigger = 0;
		update_seconds = first_update_seconds;
		printk("First trigger: second=%d, update_seconds=%d\n", second, update_seconds);
	} else {
		update_seconds = clock_interval;
	}
	
	// 重启定时器，应用修正后的间隔（单位：10ms，故乘以100）
	app_clock_timer_used = app_easy_timer(clock_interval*100+adj, app_clock_timer_cb);

	// 确定屏幕更新标志（根据时钟状态）
	int flags = UPDATE_FLY; // 默认快速更新
	// 更新时钟并打印
	int stat = clock_update(update_seconds);
	clock_print();

	// 如果已连接，则推送时钟数据
	if(app_connection_idx!=-1){
		clock_push();
	}
	
    //未进行初始化,则始终展示二维码
    if(year==2025 && month<=5){
        // 在2024年2月执行特定操作（占位符）
        QR_draw();
        user_app_adv_start();//持续开启广播
        return;
    }

    // 如果是快速更新，更新ADC数据,若电量不足则不继续执行任务
	if(flags==4){
		adc1_update();
		//ADC电压小于2.6V
        if(adcval<1360){
					//绘制低电量图标
            LB_draw();
					//清除定时器唤醒任务
					app_easy_timer_cancel(app_clock_timer_used);
            return;
        }
	}

	if(stat>=3){
		flags = DRAW_BT | UPDATE_FULL; // 需要蓝牙图标+全量更新
	}else if(stat>=2){
		flags = DRAW_BT | UPDATE_FAST; // 需要蓝牙图标+快速更新
	}

	// 如果需要显示蓝牙图标，启动广播
	if(flags&DRAW_BT){
		user_app_adv_start();
	}

	// 根据状态或标志更新屏幕显示
	if(stat>0 || flags&DRAW_BT){
		clock_draw(flags);
	}
}


/**
 ****************************************************************************************
 * @brief 重启应用时钟定时器
 *        计算到下一个整分钟的剩余秒数，设置定时器使其在整分钟时触发
 ****************************************************************************************
 */
void app_clock_timer_restart(void)
{
	app_easy_timer_cancel(app_clock_timer_used); // 取消当前定时器
	
	// 计算到下一个整分钟的剩余秒数
	int remaining_seconds = (second == 0) ? 60 : (60 - second);
	
	// 保存第一次触发时需要更新的秒数（使时钟进位到整分钟）
	first_update_seconds = remaining_seconds;
	
	// 标记为第一次触发，用于在回调中正确处理时钟更新
	first_timer_trigger = 1;
	
	// 第一次在整分钟触发，之后每60秒触发一次
	app_clock_timer_used = app_easy_timer(remaining_seconds*100, app_clock_timer_cb);
}


/**
 ****************************************************************************************
 * @brief 数据库初始化完成回调函数
 *        当GATT数据库初始化完成后调用，初始化ADC、显示时钟并启动广播
 ****************************************************************************************
 */
void user_app_on_db_init_complete( void )
{
	printk("\nuser_app_on_db_init_complete!\n");

	// 更新ADC值并打印电压
	int adcval = adc1_update();
	printk("Voltage: %d\n", adcval);

	// 打印并推送时钟数据
	clock_print();
	clock_push();

	// 绘制时钟（带蓝牙图标+全量更新）并启动广播
	//clock_draw(DRAW_BT|UPDATE_FULL);
	QR_draw();
	user_app_adv_start();

	// 启动时钟定时器，对齐到整分钟
	app_clock_timer_restart();
}


/**
 ****************************************************************************************
 * @brief 启动应用广播
 *        构造广播数据（包含设备名称和EPD版本），并启动带超时的无向广播
 ****************************************************************************************
 */
void user_app_adv_start(void)
{
	u8 vbuf[4]; // 版本信息AD结构缓冲区

	// 如果已在广播状态，直接返回
	if(adv_state)
		return;
	adv_state = 1; // 标记为正在广播

    // 获取广播命令结构
	struct gapm_start_advertise_cmd* cmd = app_easy_gap_undirected_advertise_get_active();
	// 添加设备名称AD结构
	app_add_ad_struct(cmd, adv_name, adv_name[0]+1, 1);

	// 构造版本信息AD结构（长度+类型+版本号低两位）
	vbuf[0] = 0x03;
	vbuf[1] = GAP_AD_TYPE_MANU_SPECIFIC_DATA;
	vbuf[2] = EPD_VERSION&0xff;
	vbuf[3] = (EPD_VERSION>>8)&0xff;
	app_add_ad_struct(cmd, vbuf, vbuf[0]+1, 1);

	// 启动带超时的无向广播
	app_easy_gap_undirected_advertise_with_timeout_start(user_default_hnd_conf.advertise_period, NULL);
	printk("\nuser_app_adv_start! %s\n", adv_name+2);
}


/**
 ****************************************************************************************
 * @brief 连接事件回调函数
 *        当收到连接请求时调用，更新连接索引，检查连接参数并在需要时请求参数更新
 * @param[in] connection_idx 连接索引
 * @param[in] param          连接请求参数
 ****************************************************************************************
 */
void user_app_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param)
{
	printk("user_app_connection: %d\n", connection_idx);

    // 检查连接是否有效
    if (app_env[connection_idx].conidx != GAP_INVALID_CONIDX)
    {
        app_connection_idx = connection_idx; // 更新连接索引

		// 打印连接参数
		printk("  interval: %d\n", param->con_interval);
		printk("  latency : %d\n", param->con_latency);
		printk("  sup_to  : %d\n", param->sup_to);
        
        // 检查连接参数是否符合预期，不符合则调度参数更新请求
        if ((param->con_interval < user_connection_param_conf.intv_min) ||
            (param->con_interval > user_connection_param_conf.intv_max) ||
            (param->con_latency != user_connection_param_conf.latency) ||
            (param->sup_to != user_connection_param_conf.time_out))
        {
            app_param_update_request_timer_used = app_easy_timer(APP_PARAM_UPDATE_REQUEST_TO, param_update_request_timer_cb);
        }
		
		// 推送时钟数据到客户端
		clock_push();
    } else {
		adv_state = 0; // 连接无效时，标记为未广播
    }

    // 执行默认连接处理
    default_app_on_connection(connection_idx, param);
}

/**
 ****************************************************************************************
 * @brief 无向广播完成回调函数
 *        当广播超时或异常结束时调用，更新广播状态并刷新屏幕
 * @param[in] status 广播结束状态码
 ****************************************************************************************
 */
void user_app_adv_undirect_complete(uint8_t status)
{
	printk("user_app_adv_undirect_complete: %02x\n", status);
	// 状态非0表示异常结束，更新广播状态并刷新屏幕
	if(status!=0){
		adv_state = 0;
		//未进行初始化,则始终展示二维码
    if(year==2025 && month<=5){
        // 在2024年2月执行特定操作（占位符）
        QR_draw();
    }
		else
		clock_draw(UPDATE_FLY);
	}
}


/**
 ****************************************************************************************
 * @brief 断开连接回调函数
 *        当连接断开时调用，清理定时器，更新连接状态，并根据断开原因决定是否重启广播
 * @param[in] param 断开连接参数（包含断开原因）
 ****************************************************************************************
 */
void user_app_disconnect(struct gapc_disconnect_ind const *param)
{
	printk("user_app_disconnect! reason=%02x\n", param->reason);

    // 取消参数更新请求定时器
    if (app_param_update_request_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_param_update_request_timer_used);
        app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    }

	app_connection_idx = -1; // 重置连接索引为无效值
	adv_state = 0; // 标记为未广播

	// 非远程用户主动断开时，重启广播；否则仅刷新屏幕
	if(param->reason!=CO_ERROR_REMOTE_USER_TERM_CON){
		user_app_adv_start();
	}else{
		    //未进行初始化,则始终展示二维码
    if(year==2025 && month<=5){
        // 在2024年2月执行特定操作（占位符）
        QR_draw();
    }
		else
		clock_draw(UPDATE_FLY);
	}

}


/**
 ****************************************************************************************
 * @brief 未处理消息的捕获处理函数
 *        处理各类未被默认处理的消息，包括特征值读写、参数更新、MTU变更等事件
 * @param[in] msgid   消息ID
 * @param[in] param   消息参数
 * @param[in] dest_id 目标任务ID
 * @param[in] src_id  源任务ID
 ****************************************************************************************
 */
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    switch(msgid)
    {
        // 特征值写入通知（值已写入数据库）
        case CUSTS1_VAL_WRITE_IND:
        {
            struct custs1_val_write_ind const *msg_param = (struct custs1_val_write_ind const *)(param);

            // 根据句柄分发到对应的处理函数
            switch (msg_param->handle)
            {
                case SVC1_IDX_CONTROL_POINT_VAL:
                    user_svc1_ctrl_wr_ind_handler(msgid, msg_param, dest_id, src_id);
                    break;

                case SVC1_IDX_LONG_VALUE_VAL:
                    user_svc1_long_val_wr_ind_handler(msgid, msg_param, dest_id, src_id);
                    break;

                default:
                    break;
            }
        } break;

        // Notification确认（请求已发出）
        case CUSTS1_VAL_NTF_CFM:
        {
        } break;

        // Indication确认（请求已发出）
        case CUSTS1_VAL_IND_CFM:
        {
        } break;

        // 读ATT_INFO请求（需要返回数据）
        case CUSTS1_ATT_INFO_REQ:
        {
            struct custs1_att_info_req const *msg_param = (struct custs1_att_info_req const *)param;

            // 根据属性索引分发处理
            switch (msg_param->att_idx)
            {
                case SVC1_IDX_LONG_VALUE_VAL:
                    user_svc1_long_val_att_info_req_handler(msgid, msg_param, dest_id, src_id);
                    break;

                default:
                    user_svc1_rest_att_info_req_handler(msgid, msg_param, dest_id, src_id);
                    break;
             }
        } break;

        // 连接参数更新通知
        case GAPC_PARAM_UPDATED_IND:
        {
            struct gapc_param_updated_ind const *msg_param = (struct gapc_param_updated_ind const *)(param);
			printk("GAPC_PARAM_UPDATED_IND!\n");
			// 打印更新后的参数
			printk("  interval: %d\n", msg_param->con_interval);
			printk("  latency : %d\n", msg_param->con_latency);
			printk("  sup_to  : %d\n", msg_param->sup_to);

            // 检查更新后的参数是否符合预期
            if ((msg_param->con_interval >= user_connection_param_conf.intv_min) &&
                (msg_param->con_interval <= user_connection_param_conf.intv_max) &&
                (msg_param->con_latency == user_connection_param_conf.latency) &&
                (msg_param->sup_to == user_connection_param_conf.time_out))
            {
				printk("  match!\n");
            }
        } break;

        // 特征值读取请求
        case CUSTS1_VALUE_REQ_IND:
        {
			printk("CUSTS1_VALUE_REQ_IND!\n");
            struct custs1_value_req_ind const *msg_param = (struct custs1_value_req_ind const *) param;

            // 处理未定义的读取请求，返回错误
            switch (msg_param->att_idx)
            {
                default:
                {
                    struct custs1_value_req_rsp *rsp = KE_MSG_ALLOC(CUSTS1_VALUE_REQ_RSP,
                                                                    src_id,
                                                                    dest_id,
                                                                    custs1_value_req_rsp);

                    rsp->conidx  = app_env[msg_param->conidx].conidx;
                    rsp->att_idx = msg_param->att_idx;
                    rsp->length = 0;
                    rsp->status  = ATT_ERR_APP_ERROR;
                    KE_MSG_SEND(rsp);
                } break;
             }
        } break;

        // GATT事件请求指示（确认未处理的指示以避免超时）
        case GATTC_EVENT_REQ_IND:
        {
            struct gattc_event_ind const *ind = (struct gattc_event_ind const *) param;
            struct gattc_event_cfm *cfm = KE_MSG_ALLOC(GATTC_EVENT_CFM, src_id, dest_id, gattc_event_cfm);
            cfm->handle = ind->handle;
            KE_MSG_SEND(cfm);
        } break;
		
		// MTU（最大传输单元）变更指示
		case GATTC_MTU_CHANGED_IND:
		{
			struct gattc_mtu_changed_ind *ind = (struct gattc_mtu_changed_ind *) param;
			printk("GATTC_MTU_CHANGED_IND: %d\n", ind->mtu);
		} break;

        // 未处理的消息
        default:
		{
			printk("Unhandled msgid=%08x\n", msgid);
		} break;
    }
}

/// @} APP