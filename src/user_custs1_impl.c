/**
 ****************************************************************************************
 *
 * @file user_custs1_impl.c
 *
 * @brief Peripheral project Custom1 Server implementation source code.
 *
 * Copyright (C) 2015-2023 Renesas Electronics Corporation and/or its affiliates.
 * All rights reserved. Confidential Information.
 *
 * This software ("Software") is supplied by Renesas Electronics Corporation and/or its
 * affiliates ("Renesas"). Renesas grants you a personal, non-exclusive, non-transferable,
 * revocable, non-sub-licensable right and license to use the Software, solely if used in
 * or together with Renesas products. You may make copies of this Software, provided this
 * copyright notice and disclaimer ("Notice") is included in all such copies. Renesas
 * reserves the right to change or discontinue the Software at any time without notice.
 *
 * THE SOFTWARE IS PROVIDED "AS IS". RENESAS DISCLAIMS ALL WARRANTIES OF ANY KIND,
 * WHETHER EXPRESS, IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. TO THE
 * MAXIMUM EXTENT PERMITTED UNDER LAW, IN NO EVENT SHALL RENESAS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE, EVEN IF RENESAS HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGES. USE OF THIS SOFTWARE MAY BE SUBJECT TO TERMS AND CONDITIONS CONTAINED IN
 * AN ADDITIONAL AGREEMENT BETWEEN YOU AND RENESAS. IN CASE OF CONFLICT BETWEEN THE TERMS
 * OF THIS NOTICE AND ANY SUCH ADDITIONAL LICENSE AGREEMENT, THE TERMS OF THE AGREEMENT
 * SHALL TAKE PRECEDENCE. BY CONTINUING TO USE THIS SOFTWARE, YOU AGREE TO THE TERMS OF
 * THIS NOTICE.IF YOU DO NOT AGREE TO THESE TERMS, YOU ARE NOT PERMITTED TO USE THIS
 * SOFTWARE.
 *
 ****************************************************************************************
 */

/*
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "gpio.h"               // GPIO控制相关头文件
#include "app_api.h"            // 应用程序API
#include "app.h"                // 应用程序核心功能
#include "prf_utils.h"          // BLE配置文件工具
#include "custs1.h"             // 自定义服务1
#include "custs1_task.h"        // 自定义服务1任务
#include "user_custs1_def.h"    // 自定义服务1定义
#include "user_custs1_impl.h"   // 自定义服务1实现
#include "user_peripheral.h"    // 用户外设相关
#include "user_periph_setup.h"  // 用户外设设置
#include "adc.h"                // ADC(模数转换)相关

#include "epd.h"                // 电子墨水屏驱动

/*
 * 全局变量定义
 * 这些变量使用__SECTION_ZERO("retention_mem_area0")属性放置在掉电保持内存区域
 ****************************************************************************************
 */

// 定时器ID，用于系统定时任务
ke_msg_id_t timer_used      __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// 指示计数器，用于BLE通知计数
uint16_t indication_counter __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// 非数据库值计数器
uint16_t non_db_val_counter __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// ADC采样值，用于电池电量检测
int adcval;

static uint8_t h24_format = 1; // 24小时制标志

extern int adv_state;
/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */


/**
 * @brief 更新ADC采样值并通过BLE发送电池电压数据
 * 
 * 该函数执行以下操作：
 * 1. 校准ADC偏移量
 * 2. 获取电池电压采样值
 * 3. 将采样值转换为实际电压值
 * 4. 通过BLE发送电压值给连接的设备
 * 
 * @return 返回计算后的电压值
 */
int adc1_update(void)
{
    // 校准ADC偏移，使用单端输入模式
    adc_offset_calibrate(ADC_INPUT_MODE_SINGLE_ENDED);
    // 获取电池电压采样值
    adcval = adc_get_vbat_sample(false);
    // 将ADC值转换为实际电压值 (单位: mV)
    int volt = (adcval*225)>>7;

    // 分配内存并构造BLE消息
    struct custs1_val_set_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ, 
                                                      prf_get_task_from_id(TASK_ID_CUSTS1), 
                                                      TASK_APP, 
                                                      custs1_val_set_req, 
                                                      DEF_SVC1_ADC_VAL_1_CHAR_LEN);
    // 设置连接索引
    req->conidx = app_env->conidx;
    // 设置特征值句柄
    req->handle = SVC1_IDX_ADC_VAL_1_VAL;
    // 设置数据长度
    req->length = DEF_SVC1_ADC_VAL_1_CHAR_LEN;
    // 设置电压值（16位，低字节在前）
    req->value[0] = volt&0xff;
    req->value[1] = volt>>8;
    // 发送BLE消息
    KE_MSG_SEND(req);
    
    return volt;
}


/****************************************************************************************/

/**
 * 全局时间变量定义
 * year: 年份，如2025
 * month: 月份，0-11表示1-12月
 * date: 日期，0-30表示1-31日
 * wday: 星期，0-6表示星期日到星期六
 * hour: 小时，0-23
 * minute: 分钟，0-59
 * second: 秒，0-59
 */
int year=2025, month=0, date=0, wday=3;
int hour=0, minute=0, second=0;
// 上次对时后，经过的分钟数
int cal_minute=-1;


//GUIQRLB
// QR code data (31x31, 1px quiet zone, Version 3 / EC level M), encodes the
// pairing page URL. TEMP: points at a local LAN IP (https://192.168.1.43) --
// replace once a permanent hosting location for the web app is decided.
const unsigned char QR_31x31[31][4] = {
    {0x00, 0x00, 0x00, 0x00},
    {0x7F, 0x0E, 0x65, 0xFC},
    {0x41, 0x13, 0x95, 0x04},
    {0x5D, 0x2A, 0x51, 0x74},
    {0x5D, 0x23, 0xB9, 0x74},
    {0x5D, 0x2F, 0x59, 0x74},
    {0x41, 0x4B, 0x05, 0x04},
    {0x7F, 0x55, 0x55, 0xFC},
    {0x00, 0x21, 0x3C, 0x00},
    {0x4B, 0x4C, 0xBA, 0x80},
    {0x3C, 0x0D, 0x99, 0xA4},
    {0x01, 0x54, 0x68, 0x38},
    {0x6E, 0x21, 0xA4, 0xD8},
    {0x4D, 0x14, 0x41, 0x2C},
    {0x16, 0x24, 0xA8, 0x80},
    {0x63, 0xC8, 0xF3, 0xBC},
    {0x50, 0x47, 0x7F, 0xA8},
    {0x2D, 0x83, 0xD8, 0x88},
    {0x3A, 0x50, 0xB5, 0x24},
    {0x55, 0xD6, 0x24, 0xCC},
    {0x1E, 0x8E, 0xCF, 0xCC},
    {0x55, 0xE1, 0x6F, 0xD0},
    {0x00, 0x4A, 0xEC, 0x5C},
    {0x7F, 0x3F, 0x95, 0x48},
    {0x41, 0x7A, 0x5C, 0x78},
    {0x5D, 0x05, 0x97, 0xCC},
    {0x5D, 0x67, 0xDB, 0x74},
    {0x5D, 0x2E, 0x00, 0xF4},
    {0x41, 0x22, 0x85, 0x28},
    {0x7F, 0x50, 0x0D, 0x48},
    {0x00, 0x00, 0x00, 0x00},
};

const unsigned char LB_31x31[31][4] = {
    {0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00},
    {0x00, 0x07, 0xC0, 0x00},
    {0x00, 0x04, 0x40, 0x00},
    {0x00, 0xFF, 0xFE, 0x00},
    {0x00, 0x80, 0x02, 0x00},
    {0x01, 0x80, 0x03, 0x00},
    {0x01, 0x00, 0x01, 0x00},
    {0x01, 0x00, 0x01, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x00, 0x01, 0x00},
    {0x01, 0x00, 0x01, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x03, 0x81, 0x00},
    {0x01, 0x00, 0x01, 0x00},
    {0x01, 0x00, 0x01, 0x00},
    {0x01, 0xC0, 0x07, 0x00},
    {0x00, 0x40, 0x04, 0x00},
    {0x00, 0x7F, 0xFC, 0x00},
    {0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00},
};


/****************************************************************************************/


static int get_month_day(int mon)
{
	uint8_t d2m[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
	int is_leap = (year%4)? 0 : (year%100)? 1: (year%400)? 0: 1;
	d2m[1] += is_leap;

	return d2m[month];
}


// 增加1天
void date_inc(void)
{
	wday += 1;
	if(wday>=7)
		wday = 0;
	
	date += 1;
	if(date==get_month_day(month)){
		date = 0;
		month += 1;
		if(month>=12){
			month = 0;
			year += 1;
		}
	}
}

// 0: 状态不变
// 1: 分钟改变
// 2: 分钟改变10分钟
// 3: 小时改变
// 4: 天数改变

int clock_update(int inc)
{
	int retv = 0;

	second += inc;
	if(second<60)
		return retv;
	second -= 60;

	minute += 1;
	retv = 1;
	if((minute%10)==0)
		retv = 2;

	if(cal_minute>=0)
		cal_minute += 1;

	if(minute>=60){
		minute = 0;
		hour += 1;
		retv = 3;
		if(hour>=24){
			hour = 0;
			date_inc();
			retv = 4;
		}
	}

	return retv;
}

void clock_set(uint8_t *buf)
{
	int new_year   = buf[1] + buf[2]*256;
	int new_month  = buf[3];
	int new_date   = buf[4]-1;
	int new_hour   = buf[5];
	int new_minute = buf[6];
	int new_second = buf[7];
	int new_wday   = buf[8];

	// Reject out-of-range values -- this data comes straight from an
	// unauthenticated BLE write.
	if(new_month<0 || new_month>11)   return;
	if(new_date<0  || new_date>30)    return;
	if(new_hour<0  || new_hour>23)    return;
	if(new_minute<0 || new_minute>59) return;
	if(new_second<0 || new_second>59) return;
	if(new_wday<0  || new_wday>6)     return;

	year   = new_year;
	month  = new_month;
	date   = new_date;
	hour   = new_hour;
	minute = new_minute;
	second = new_second;
	wday   = new_wday;

	cal_minute = 0;

	app_clock_timer_restart();
}


void clock_push(void)
{
	struct custs1_val_set_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ, prf_get_task_from_id(TASK_ID_CUSTS1), TASK_APP, custs1_val_set_req, 11);

	req->conidx = app_env->conidx;
	req->handle = SVC1_IDX_LONG_VALUE_VAL;
	req->length = 11;
	req->value[0] = year&0xff;
	req->value[1] = year>>8;
	req->value[2] = month;
	req->value[3] = date+1;
	req->value[4] = hour;
	req->value[5] = minute;
	req->value[6] = second;
	req->value[7] = (cal_minute&0xff);
	req->value[8] = (cal_minute>>8 )&0xff;
	req->value[9] = (cal_minute>>16)&0xff;
	req->value[10]= (cal_minute>>24)&0xff;
	KE_MSG_SEND(req);
}


void clock_print(void)
{
	printk("\n%04d-%02d-%02d %02d:%02d:%02d\n", year, month+1, date+1, hour, minute, second);
}


/****************************************************************************************/

static char *wday_str[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

static int epd_wait_state;
static timer_hnd epd_wait_hnd;


/****************************************************************************************/


static uint8_t batt_cal(uint16_t adc_sample)
{
    uint8_t batt_lvl;

    if (adc_sample > 1705)
        batt_lvl = 100;
    else if (adc_sample <= 1705 && adc_sample > 1584)
        batt_lvl = 28 + (uint8_t)(( ( ((adc_sample - 1584) << 16) / (1705 - 1584) ) * 72 ) >> 16) ;
    else if (adc_sample <= 1584 && adc_sample > 1360)
        batt_lvl = 4 + (uint8_t)(( ( ((adc_sample - 1360) << 16) / (1584 - 1360) ) * 24 ) >> 16) ;
    else if (adc_sample <= 1360 && adc_sample > 1136)
        batt_lvl = (uint8_t)(( ( ((adc_sample - 1136) << 16) / (1360 - 1136) ) * 4 ) >> 16) ;
    else
        batt_lvl = 0;

    return batt_lvl;
}


/**
 * 绘制电池电量图标
 * 
 * @param x 图标左上角的x坐标
 * @param y 图标中心的y坐标
 * 
 * 图标说明：
 * - 外框大小：16x8像素
 * - 电量显示：根据实际电量百分比填充内部
 * - 电池正极：2x2像素
 */
static void draw_batt(int x, int y)
{
    // 获取电池电量百分比并转换为显示段数（0-10）
    int p = batt_cal(adcval);
    p /= 10;

    // 绘制电池外框
    draw_rect(x, y-4, x+14, y+4, BLACK);
    // 绘制电池正极
    draw_box(x-2, y-1, x-1, y+1, BLACK);

    // 绘制电量填充部分
    draw_box(x+12-p, y-2, x+12, y+2, BLACK);
}


const u8 font_bt[] = {
	0x08, 0x08, 0x0f, 0x00, 0x00,
	0x10, 0x18, 0x14, 0x92, 0x51, 0x32, 0x14, 0x18,
	0x14, 0x32, 0x51, 0x92, 0x14, 0x18, 0x10,
};

/**
 * 绘制蓝牙图标
 * 
 * @param x 图标中心的x坐标
 * @param y 图标中心的y坐标
 * 
 * 图标说明: 一个8x15的字符
 */
static void draw_bt(int x, int y)
{
	fb_draw_font_info(x, y, font_bt, BLACK);
}


/****************************************************************************************/

typedef struct {
	int xres, yres;
	int font_char;
	int font_dseg;
	u16 x[8];
	u16 y[8];
}LAYOUT;

// 坐标0: 日期
// 坐标1: 蓝牙图标
// 坐标2: 电池图标
// 坐标3: 时间
// 坐标4: 未使用（原农历日期，已移除）
// 坐标5: 未使用（原节气，已移除）
// 坐标6: 设备ID（仅蓝牙广播时显示）
// 坐标7: 上下午

LAYOUT layouts[3] = {
	{212, 104, 0, 1,
		{15, 172, 190,  16,  12,  98, 150, 12},
		{ 6,   7,  14,  27,  82,  82,  82, 44},
	},
	{250, 122, 2, 3,
		{15, 206, 226,  12,  12, 118, 176, 15},
		{ 6,   8,  15,  28,  98,  98,  98, 50},
	},
	{296, 128, 2, 3,
		{15, 246, 268,  30,  12, 140, 220, 15,},
		{ 6,   8,  15,  30, 102, 102, 102, 52,},
	},
};

int current_layout = 0;

void select_layout(int xres, int yres)
{
	int i;

	for(i=0; i<3; i++){
		if(layouts[i].xres==xres && layouts[i].yres==yres){
			current_layout = i;
			return;
		}
	}
}


/**
 * 电子墨水屏更新等待定时器
 * 
 * 功能说明：
 * - 检查电子墨水屏是否处于忙状态
 * - 如果忙，则40ms后再次检查
 * - 如果空闲，则完成更新流程并进入省电模式
 * 
 * 电子墨水屏更新完成后的处理：
 * 1. 发送深度睡眠命令(0x10, 0x01)
 * 2. 关闭电源
 * 3. 关闭硬件接口
 * 4. 设置系统进入扩展睡眠模式
 */
static void epd_wait_timer(void)
{
    if(epd_busy()){
        // 屏幕仍在忙，40ms后再次检查
        epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
    }else{
        // 屏幕更新完成
        epd_wait_hnd = EASY_TIMER_INVALID_TIMER;
        // 发送深度睡眠命令
        epd_cmd1(0x10, 0x01);
        // 关闭电源
        epd_power(0);
        // 关闭硬件接口
        epd_hw_close();
        // 设置系统进入扩展睡眠模式
        arch_set_sleep_mode(ARCH_EXT_SLEEP_ON);
    }
}


void QR_draw()
{
	char tbuf[16];

	// 此处添加QR码绘制逻辑
	epd_hw_open();

	epd_update_mode(UPDATE_FULL);

	memset(fb_bw, 0xff, scr_h*line_bytes);
	memset(fb_rr, 0x00, scr_h*line_bytes);

	draw_qr_code(5, 5, 3, QR_31x31);
	draw_text(100, 5,"Bluetooth", BLACK);
	// 设备名一次性绘制为单个连续字符串（DCLK-XXYYZZ），避免拆成两次draw_text
	// 在固定x坐标下产生视觉断行。
	sprintf(tbuf, "DCLK-%s", bt_id);
	draw_text(100, 20, tbuf, BLACK);

	draw_text(110,40,"-------------",BLACK);
	
	draw_text(100, 60, "Scan the QR code", BLACK);
	draw_text(100, 75, "with your browser", BLACK);
	// 墨水屏更新显示
	epd_init();
	epd_screen_update();
	epd_update();
	// 更新时如果深度休眠，会花屏。 这里暂时关闭休眠。
	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
}

void LB_draw()
{
	// 此处添加低电压码的绘制逻辑
	epd_hw_open();

	epd_update_mode(UPDATE_FULL);

	memset(fb_bw, 0xff, scr_h*line_bytes);
	memset(fb_rr, 0x00, scr_h*line_bytes);	

	draw_qr_code(60, 10, 4, LB_31x31);

	// 墨水屏更新显示
	epd_init();
	epd_screen_update();
	epd_update();
	// 更新时如果深度休眠，会花屏。 这里暂时关闭休眠。
	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
}

/**
 * 绘制时钟界面
 * 
 * @param flags 显示控制标志
 *             bit0-1: 更新模式（快速/正常）
 *             bit2: 是否显示蓝牙图标
 * 
 * 显示内容包括：
 * - 电池电量图标
 * - 蓝牙连接状态图标（可选）
 * - 大字号时间显示
 * - 日期和星期（英文）
 */
void clock_draw(int flags)
{
	char tbuf[64];
	LAYOUT *lt = &layouts[current_layout];

	if(ota_state){
		return;
	}

	epd_hw_open();

	epd_update_mode(flags&3);

	memset(fb_bw, 0xff, scr_h*line_bytes);
	memset(fb_rr, 0x00, scr_h*line_bytes);

	// 显示电池电量
	draw_batt(lt->x[2], lt->y[2]);
	if(flags&DRAW_BT){
		// 显示蓝牙图标
		draw_bt(lt->x[1], lt->y[1]);
	}

	// 使用大字显示时间
	if(h24_format){
		// 24小时制
		select_font(lt->font_dseg);
		sprintf(tbuf, "%02d:%02d", hour, minute);
		draw_text(lt->x[3], lt->y[3], tbuf, BLACK);
	}else{
		// 12小时制
		int h = hour;
		int ampm = 0;
		if(h>=12){
			if(h>12)
				h -= 12;
			ampm = 1;
		}else if(h==0){
			h = 12; // 0点显示为12点
		}
		select_font(lt->font_dseg);
		sprintf(tbuf, "%2d:%02d", h, minute);
		draw_text(lt->x[3], lt->y[3], tbuf, BLACK);

		// 显示上午/下午
		select_font(lt->font_char);
		if(ampm){
			strcpy(tbuf, "PM");
		}else{
			strcpy(tbuf, "AM");
		}
		draw_text(lt->x[7], lt->y[7], tbuf, BLACK);
	}

	// 显示日期（ISO格式 + 星期全称）
	sprintf(tbuf, "%04d-%02d-%02d  %s", year, month+1, date+1, wday_str[wday]);
	select_font(lt->font_char);
	draw_text(lt->x[0], lt->y[0], tbuf, BLACK);

	// 蓝牙广播时在原节日位置显示设备ID后缀
	if(flags&DRAW_BT){
		draw_text(lt->x[6], lt->y[6], bt_id, BLACK);
	}

	// 墨水屏更新显示
	epd_init();
	epd_screen_update();
	epd_update();
	// 更新时如果深度休眠，会花屏。 这里暂时关闭休眠。
	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
}


/****************************************************************************************/


/**
 * 控制点写入指示处理函数
 * 
 * @param msgid 消息ID
 * @param param 写入参数
 * @param dest_id 目标任务ID
 * @param src_id 源任务ID
 * 
 * 处理通过BLE接收到的控制命令
 */
void user_svc1_ctrl_wr_ind_handler(ke_msg_id_t const msgid, 
                                  struct custs1_val_write_ind const *param, 
                                  ke_task_id_t const dest_id, 
                                  ke_task_id_t const src_id)
{
    // 打印接收到的控制命令
    printk("Control Point: %02x\n", param->value[0]);
}

/**
 * 长值特征写入指示处理函数
 * 
 * @param msgid 消息ID
 * @param param 写入参数
 * @param dest_id 目标任务ID
 * @param src_id 源任务ID
 * 
 * 处理命令：
 * - 0x91: 时钟设置命令
 * - 0xA0及以上: OTA升级相关命令
 */
void user_svc1_long_val_wr_ind_handler(ke_msg_id_t const msgid,
                                      struct custs1_val_write_ind const *param,
                                      ke_task_id_t const dest_id,
                                      ke_task_id_t const src_id)
{
	int len = param->length;

	if(len<1)
		return;

	if(param->value[0]==0x91){
		// 设置时钟 (至少需要buf[1..8]，共9字节)
		if(len<9) return;
		clock_set((uint8_t*)param->value);
		// 更新显示（带蓝牙图标，快速更新模式）
		clock_draw(DRAW_BT|UPDATE_FAST);
		// 打印当前时间信息
		clock_print();
	}else if(param->value[0]==0x90){
		//修改24-12小时制
		h24_format = !h24_format;
		clock_draw(DRAW_BT|UPDATE_FAST);
	}else if(param->value[0]==0x92){
		// 时间校准 (至少需要buf[1..2]，共3字节)
		if(len<3) return;
		int diff_sec;
		diff_sec  = param->value[1];
		diff_sec |= param->value[2]<<8;
		diff_sec  = (diff_sec<<16)>>16;
		printk("Calibration: %02x\n", diff_sec);
		clock_fixup_set(diff_sec, cal_minute);
		cal_minute = 0;
	}else if(param->value[0]>=0xa0){
		// 处理OTA升级命令
		ota_handle((u8*)param->value, len);
    }
}

/**
 * 长值特征属性信息请求处理函数
 * 
 * @param msgid 消息ID
 * @param param 请求参数
 * @param dest_id 目标任务ID
 * @param src_id 源任务ID
 * 
 * 响应BLE客户端的属性信息请求
 */
void user_svc1_long_val_att_info_req_handler(ke_msg_id_t const msgid, 
                                            struct custs1_att_info_req const *param, 
                                            ke_task_id_t const dest_id, 
                                            ke_task_id_t const src_id)
{
    // 分配响应消息内存
    struct custs1_att_info_rsp *rsp = KE_MSG_ALLOC(CUSTS1_ATT_INFO_RSP, 
                                                   src_id, 
                                                   dest_id, 
                                                   custs1_att_info_rsp);
    // 设置连接索引
    rsp->conidx  = app_env[param->conidx].conidx;
    // 设置属性索引
    rsp->att_idx = param->att_idx;
    // 设置长度为0
    rsp->length  = 0;
    // 设置状态为无错误
    rsp->status  = ATT_ERR_NO_ERROR;

    // 发送响应消息
    KE_MSG_SEND(rsp);
}

void user_svc1_rest_att_info_req_handler(ke_msg_id_t const msgid,
                                            struct custs1_att_info_req const *param,
                                            ke_task_id_t const dest_id,
                                            ke_task_id_t const src_id)
{
    struct custs1_att_info_rsp *rsp = KE_MSG_ALLOC(CUSTS1_ATT_INFO_RSP,
                                                   src_id,
                                                   dest_id,
                                                   custs1_att_info_rsp);
    // Provide the connection index.
    rsp->conidx  = app_env[param->conidx].conidx;
    // Provide the attribute index.
    rsp->att_idx = param->att_idx;
    // Force current length to zero.
    rsp->length  = 0;
    // Provide the ATT error code.
    rsp->status  = ATT_ERR_WRITE_NOT_PERMITTED;

    KE_MSG_SEND(rsp);
}

