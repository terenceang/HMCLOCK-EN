/**
 ****************************************************************************************
 *
 * @file user_peripheral.c
 *
 * @brief Peripheral project source file, implementing BLE peripheral functionality,
 *        clock management, EPD screen display, OTP data reading, and related features
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
 * INCLUDE FILES
 ****************************************************************************************
 */

#include "rwip_config.h"             // Software configuration
#include "gattc_task.h"              // GATT client task related definitions
#include "gap.h"                     // GAP layer related definitions
#include "app_easy_timer.h"          // Application-layer timer functionality
#include "user_peripheral.h"         // This file's interface declarations
#include "user_custs1_impl.h"        // Custom Server 1 implementation
#include "user_custs1_def.h"         // Custom Server 1 definitions
#include "co_bt.h"                   // Bluetooth protocol related definitions
#include "hw_otpc.h"                 // OTP controller hardware interface

#include "epd.h"                     // EPD e-paper display driver

/*
 * TYPE DEFINITIONS
 ****************************************************************************************
 */


/*
 * GLOBAL VARIABLE DEFINITIONS
 ****************************************************************************************
 */

int app_connection_idx                          __SECTION_ZERO("retention_mem_area0"); // Connection index, saved in the retention memory area
timer_hnd app_clock_timer_used                  __SECTION_ZERO("retention_mem_area0"); // Clock timer handle, saved in retention memory
timer_hnd app_param_update_request_timer_used   __SECTION_ZERO("retention_mem_area0"); // Parameter-update-request timer handle, saved in retention memory

int adv_state = 0;                          // Advertising state: 0 = not advertising, 1 = advertising
static int otp_btaddr[2];                      // Bluetooth address read from OTP
static int otp_boot;                           // Boot-related data read from OTP
static char adv_name[20];                      // Advertised name buffer
char *bt_id = adv_name+7;                      // Start of the Bluetooth ID within the advertised name (right after "DCLK-")
int clock_interval;                            // Clock update interval (seconds)
int clock_fixup_value;                         // Clock drift correction value
int clock_fixup_count;                         // Clock drift correction counter
static int first_timer_trigger = 0;            // Flag: whether this is the first timer trigger (used for minute-boundary alignment)
static int first_update_seconds = 0;           // Seconds to advance by on the first trigger

// EPD version info (volatile ensures it isn't optimized away; used for version detection)
const volatile u32 epd_version[3] = {0xF9A51379, ~0xF9A51379, EPD_VERSION};

extern int year,month; // Current time variables
extern int second; // Current seconds value, used to compute the remaining time to the next minute boundary

/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
*/


/**
 ****************************************************************************************
 * @brief Add an AD structure to the advertising or scan response data of a
 *        GAPM_START_ADVERTISE_CMD parameter structure
 * @param[in] cmd               GAPM_START_ADVERTISE_CMD parameter structure
 * @param[in] ad_struct_data    AD structure data buffer
 * @param[in] ad_struct_len     AD structure length
 * @param[in] adv_connectable   whether this is a connectable advertising event, which
 *                              controls the max advertising data length (28 bytes if
 *                              connectable, otherwise 31 bytes)
 ****************************************************************************************
 */
static void app_add_ad_struct(struct gapm_start_advertise_cmd *cmd, void *ad_struct_data, uint8_t ad_struct_len, uint8_t adv_connectable)
{
    // Determine the max advertising data length based on whether it's connectable
    uint8_t adv_data_max_size = (adv_connectable) ? (ADV_DATA_LEN - 3) : (ADV_DATA_LEN);

    // Prefer adding to the advertising data
    if ((adv_data_max_size - cmd->info.host.adv_data_len) >= ad_struct_len)
    {
        memcpy(&cmd->info.host.adv_data[cmd->info.host.adv_data_len], ad_struct_data, ad_struct_len);
        cmd->info.host.adv_data_len += ad_struct_len;
    }
    // If there's not enough room in the advertising data, add to the scan response data
    else if ((SCAN_RSP_DATA_LEN - cmd->info.host.scan_rsp_data_len) >= ad_struct_len)
    {
        memcpy(&cmd->info.host.scan_rsp_data[cmd->info.host.scan_rsp_data_len], ad_struct_data, ad_struct_len);
        cmd->info.host.scan_rsp_data_len += ad_struct_len;
    }
    // Trigger an assertion warning if there's no room left
    else
    {
        ASSERT_WARNING(0);
    }
}


/**
 ****************************************************************************************
 * @brief Parameter-update-request timer callback
 *        Issues a connection parameter update request once the timer expires
 ****************************************************************************************
*/
static void param_update_request_timer_cb()
{
    app_easy_gap_param_update_start(app_connection_idx);  // Issue the parameter update request
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;  // Reset the timer handle
}


/**
 ****************************************************************************************
 * @brief Read values from OTP (One-Time Programmable) memory
 *        Primarily reads the Bluetooth address and boot info, and builds the advertised name
 ****************************************************************************************
 */
static void read_otp_value(void)
{
	hw_otpc_init();               // Initialize the OTP controller
	hw_otpc_manual_read_on(false); // Disable manual read mode

	// Read data from specific OTP addresses
	otp_boot = *(u32*)(0x07f8fe00);    // Read boot-related data
	otp_btaddr[0] = *(u32*)(0x07f8ffa8); // Read the low 32 bits of the Bluetooth address
	otp_btaddr[1] = *(u32*)(0x07f8ffac); // Read the high 32 bits of the Bluetooth address

	hw_otpc_disable();            // Disable the OTP controller

	// Process the Bluetooth address to derive a unique device identifier
	u32 ba0 = otp_btaddr[0];
	u32 ba1 = otp_btaddr[1];

	ba1 = (ba1<<8)|(ba0>>24);
	ba0 &= 0x00ffffff;
	ba0 ^= ba1;

	// Build the advertised name (format: DCLK-XXYYZZ, where XXYYZZ are the last
	// three bytes of the Bluetooth address)
	u8 *ba = (u8*)&ba0;
	sprintf(adv_name+2, "DCLK-%02x%02x%02x", ba[2], ba[1], ba[0]);
	int name_len = strlen(adv_name+2);

	// If the device name hasn't been set, use the generated name
	if(device_info.dev_name.length==0){
		device_info.dev_name.length = name_len;
		memcpy(device_info.dev_name.name, adv_name+2, name_len);
	}

	// Build the AD structure: first byte is the length, second byte is the AD
	// type (complete name)
	adv_name[0] = name_len+1;
	adv_name[1] = GAP_AD_TYPE_COMPLETE_NAME;
}

// Externally declared region table base address (used for memory-related operations)
extern int Region$$Table$$Base;

/**
 ****************************************************************************************
 * @brief Application initialization function
 *        Initializes OTP data, timers, the screen, Bluetooth, and other modules
 ****************************************************************************************
 */
void user_app_init(void)
{
	read_otp_value();  // Read OTP data and build the advertised name

	printk("\n\nuser_app_init! %s %08x\n", __TIME__, epd_version[2]);
    app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;  // Initialize the parameter-update timer
	app_clock_timer_used = EASY_TIMER_INVALID_TIMER;                 // Initialize the clock timer

	clock_interval = 60; // Set the clock update interval to 60 seconds
	clock_fixup_value = 0; // Initialize the clock drift correction value
	clock_fixup_count = 0; // Initialize the clock drift correction counter

	first_timer_trigger = 0; // Initialize the first-trigger flag

	adv_state = 0; // Initialize to the not-advertising state
	fspi_config(0x00030605); // Configure the FSPI interface

	selflash(otp_boot); // Perform the self-flash operation based on the OTP boot data

	// Initialize the EPD screen (2.13" black/white, 6 test points)
	epd_hw_init(0x23200700, 0x05210006, detect_w, detect_h, detect_mode | ROTATE_3);
	if(epd_detect()==0){  // If the screen isn't detected, try the other pinout (5 test points)
		epd_hw_init(0x23111000, 0x07210120, detect_w, detect_h, detect_mode | ROTATE_3);
		epd_detect();
	}

	app_connection_idx = -1; // Initialize the connection index to invalid
    default_app_on_init();   // Run the default application initialization
}


/**
 ****************************************************************************************
 * @brief Clock drift correction function
 * @param[in] diff_sec  the error in seconds since the last time sync (positive = fast, negative = slow)
 * @param[in] minutes   minutes elapsed since the last time sync
 * @note Computes and accumulates the clock correction value, used to adjust the timer
 *       interval and compensate for clock drift
 ****************************************************************************************
 */
void clock_fixup_set(int diff_sec, int minutes)
{
	// Compute the new correction value (fixed-point, 4096 = 1.0)
	int new_fixup_value = diff_sec*100*4096/minutes;
	clock_fixup_value += new_fixup_value; // Accumulate the correction value
}


/**
 ****************************************************************************************
 * @brief Apply the clock correction value
 * @return the number of milliseconds to adjust by this time
 * @note Extracts the integer part from the accumulated correction counter as this
 *       cycle's adjustment, keeping the remainder for next time
 ****************************************************************************************
 */
static int clock_fixup(void)
{
	int value;

	clock_fixup_count += clock_fixup_value; // Accumulate the correction counter

	value = clock_fixup_count>>12; // Shift right by 12 bits (divide by 4096) to get the integer part
	clock_fixup_count &= 0xfff;    // Keep the low 12 bits as the remainder

	return value; // Return this cycle's adjustment in milliseconds
}

extern int adcval;  // ADC voltage value variable
/**
 ****************************************************************************************
 * @brief Application clock timer callback
 *        Periodically updates the clock, pushes clock data, handles the screen
 *        display, and restarts advertising when needed
 ****************************************************************************************
 */
static void app_clock_timer_cb(void)
{
	int adj = clock_fixup(); // Get the clock correction value
	int update_seconds;

	if(first_timer_trigger) {
		first_timer_trigger = 0;
		update_seconds = first_update_seconds;
		printk("First trigger: second=%d, update_seconds=%d\n", second, update_seconds);
	} else {
		update_seconds = clock_interval;
	}

	// Restart the timer, applying the corrected interval (units: 10ms, hence x100)
	app_clock_timer_used = app_easy_timer(clock_interval*100+adj, app_clock_timer_cb);

	// Determine the screen update flags (based on clock state)
	int flags = UPDATE_FLY; // Fast update by default
	// Update the clock and print it
	int stat = clock_update(update_seconds);
	clock_print();

	// If connected, push the clock data
	if(app_connection_idx!=-1){
		clock_push();
	}

    // Not yet configured -- always show the pairing QR code
    if(year==2025 && month<=5){
        // Placeholder gate for "not configured yet"
        QR_draw();
        user_app_adv_start(); // Keep advertising continuously
        return;
    }

    // On a fast-update tick, refresh the ADC reading; stop further work if the battery is low
	if(flags==4){
		adc1_update();
		// ADC voltage below 2.6V
        if(adcval<1360){
					// Draw the low-battery icon
            LB_draw();
					// Cancel the timer so it stops waking the device
					app_easy_timer_cancel(app_clock_timer_used);
            return;
        }
	}

	if(stat>=3){
		flags = DRAW_BT | UPDATE_FULL; // Needs the Bluetooth icon + a full update
	}else if(stat>=2){
		flags = DRAW_BT | UPDATE_FAST; // Needs the Bluetooth icon + a fast update
	}

	// Start advertising if the Bluetooth icon needs to be shown
	if(flags&DRAW_BT){
		user_app_adv_start();
	}

	// Update the screen based on the state or flags
	if(stat>0 || flags&DRAW_BT){
		clock_draw(flags);
	}
}


/**
 ****************************************************************************************
 * @brief Restart the application clock timer
 *        Computes the remaining seconds to the next minute boundary and arms the
 *        timer to fire exactly on that boundary
 ****************************************************************************************
 */
void app_clock_timer_restart(void)
{
	app_easy_timer_cancel(app_clock_timer_used); // Cancel the current timer

	// Compute the remaining seconds to the next minute boundary
	int remaining_seconds = (second == 0) ? 60 : (60 - second);

	// Save the number of seconds to advance by on the first trigger (so the
	// clock lands exactly on a minute boundary)
	first_update_seconds = remaining_seconds;

	// Mark this as the first trigger, so the callback handles the clock
	// update correctly
	first_timer_trigger = 1;

	// Fires first on the minute boundary, then every 60 seconds after that
	app_clock_timer_used = app_easy_timer(remaining_seconds*100, app_clock_timer_cb);
}


/**
 ****************************************************************************************
 * @brief Database initialization complete callback
 *        Called once the GATT database has finished initializing; initializes the
 *        ADC, draws the clock, and starts advertising
 ****************************************************************************************
 */
void user_app_on_db_init_complete( void )
{
	printk("\nuser_app_on_db_init_complete!\n");

	// Update the ADC value and print the voltage
	int adcval = adc1_update();
	printk("Voltage: %d\n", adcval);

	// Print and push the clock data
	clock_print();
	clock_push();

	// Draw the clock (with Bluetooth icon + full update) and start advertising
	//clock_draw(DRAW_BT|UPDATE_FULL);
	QR_draw();
	user_app_adv_start();

	// Start the clock timer, aligned to the minute boundary
	app_clock_timer_restart();
}


/**
 ****************************************************************************************
 * @brief Start application advertising
 *        Builds the advertising data (device name + EPD version) and starts
 *        undirected advertising with a timeout
 ****************************************************************************************
 */
void user_app_adv_start(void)
{
	u8 vbuf[4]; // Version-info AD structure buffer

	// Return immediately if already advertising
	if(adv_state)
		return;
	adv_state = 1; // Mark as advertising

    // Get the advertising command structure
	struct gapm_start_advertise_cmd* cmd = app_easy_gap_undirected_advertise_get_active();
	// Add the device-name AD structure
	app_add_ad_struct(cmd, adv_name, adv_name[0]+1, 1);

	// Build the version-info AD structure (length + type + low two version bytes)
	vbuf[0] = 0x03;
	vbuf[1] = GAP_AD_TYPE_MANU_SPECIFIC_DATA;
	vbuf[2] = EPD_VERSION&0xff;
	vbuf[3] = (EPD_VERSION>>8)&0xff;
	app_add_ad_struct(cmd, vbuf, vbuf[0]+1, 1);

	// Start undirected advertising with a timeout
	app_easy_gap_undirected_advertise_with_timeout_start(user_default_hnd_conf.advertise_period, NULL);
	printk("\nuser_app_adv_start! %s\n", adv_name+2);
}


/**
 ****************************************************************************************
 * @brief Connection event callback
 *        Called when a connection request is received; updates the connection index,
 *        checks the connection parameters, and requests a parameter update if needed
 * @param[in] connection_idx connection index
 * @param[in] param          connection request parameters
 ****************************************************************************************
 */
void user_app_connection(uint8_t connection_idx, struct gapc_connection_req_ind const *param)
{
	printk("user_app_connection: %d\n", connection_idx);

    // Check whether the connection is valid
    if (app_env[connection_idx].conidx != GAP_INVALID_CONIDX)
    {
        app_connection_idx = connection_idx; // Update the connection index

		// Print the connection parameters
		printk("  interval: %d\n", param->con_interval);
		printk("  latency : %d\n", param->con_latency);
		printk("  sup_to  : %d\n", param->sup_to);

        // Check whether the connection parameters match expectations; if not,
        // schedule a parameter update request
        if ((param->con_interval < user_connection_param_conf.intv_min) ||
            (param->con_interval > user_connection_param_conf.intv_max) ||
            (param->con_latency != user_connection_param_conf.latency) ||
            (param->sup_to != user_connection_param_conf.time_out))
        {
            app_param_update_request_timer_used = app_easy_timer(APP_PARAM_UPDATE_REQUEST_TO, param_update_request_timer_cb);
        }

		// Push the clock data to the client
		clock_push();
    } else {
		adv_state = 0; // Mark as not advertising if the connection is invalid
    }

    // Run the default connection handling
    default_app_on_connection(connection_idx, param);
}

/**
 ****************************************************************************************
 * @brief Undirected advertising complete callback
 *        Called when advertising times out or ends abnormally; updates the
 *        advertising state and refreshes the screen
 * @param[in] status advertising end status code
 ****************************************************************************************
 */
void user_app_adv_undirect_complete(uint8_t status)
{
	printk("user_app_adv_undirect_complete: %02x\n", status);
	// A non-zero status means it ended abnormally; update the advertising
	// state and refresh the screen
	if(status!=0){
		adv_state = 0;
		// Not yet configured -- always show the pairing QR code
    if(year==2025 && month<=5){
        // Placeholder gate for "not configured yet"
        QR_draw();
    }
		else
		clock_draw(UPDATE_FLY);
	}
}


/**
 ****************************************************************************************
 * @brief Disconnect callback
 *        Called when the connection is dropped; cleans up timers, updates the
 *        connection state, and decides whether to restart advertising based on
 *        the disconnect reason
 * @param[in] param disconnect parameters (includes the disconnect reason)
 ****************************************************************************************
 */
void user_app_disconnect(struct gapc_disconnect_ind const *param)
{
	printk("user_app_disconnect! reason=%02x\n", param->reason);

    // Cancel the parameter-update-request timer
    if (app_param_update_request_timer_used != EASY_TIMER_INVALID_TIMER)
    {
        app_easy_timer_cancel(app_param_update_request_timer_used);
        app_param_update_request_timer_used = EASY_TIMER_INVALID_TIMER;
    }

	app_connection_idx = -1; // Reset the connection index to invalid
	adv_state = 0; // Mark as not advertising

	// Restart advertising unless the remote user initiated the disconnect;
	// otherwise just refresh the screen
	if(param->reason!=CO_ERROR_REMOTE_USER_TERM_CON){
		user_app_adv_start();
	}else{
		    // Not yet configured -- always show the pairing QR code
    if(year==2025 && month<=5){
        // Placeholder gate for "not configured yet"
        QR_draw();
    }
		else
		clock_draw(UPDATE_FLY);
	}

}


/**
 ****************************************************************************************
 * @brief Catch-all handler for unhandled messages
 *        Handles various messages not covered by the default handlers, including
 *        characteristic reads/writes, parameter updates, MTU changes, and more
 * @param[in] msgid   message ID
 * @param[in] param   message parameters
 * @param[in] dest_id destination task ID
 * @param[in] src_id  source task ID
 ****************************************************************************************
 */
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    switch(msgid)
    {
        // Characteristic value write indication (value has been written to the database)
        case CUSTS1_VAL_WRITE_IND:
        {
            struct custs1_val_write_ind const *msg_param = (struct custs1_val_write_ind const *)(param);

            // Dispatch to the appropriate handler based on the handle
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

        // Notification confirm (request has been sent)
        case CUSTS1_VAL_NTF_CFM:
        {
        } break;

        // Indication confirm (request has been sent)
        case CUSTS1_VAL_IND_CFM:
        {
        } break;

        // ATT_INFO read request (a response is required)
        case CUSTS1_ATT_INFO_REQ:
        {
            struct custs1_att_info_req const *msg_param = (struct custs1_att_info_req const *)param;

            // Dispatch based on the attribute index
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

        // Connection parameters updated indication
        case GAPC_PARAM_UPDATED_IND:
        {
            struct gapc_param_updated_ind const *msg_param = (struct gapc_param_updated_ind const *)(param);
			printk("GAPC_PARAM_UPDATED_IND!\n");
			// Print the updated parameters
			printk("  interval: %d\n", msg_param->con_interval);
			printk("  latency : %d\n", msg_param->con_latency);
			printk("  sup_to  : %d\n", msg_param->sup_to);

            // Check whether the updated parameters match expectations
            if ((msg_param->con_interval >= user_connection_param_conf.intv_min) &&
                (msg_param->con_interval <= user_connection_param_conf.intv_max) &&
                (msg_param->con_latency == user_connection_param_conf.latency) &&
                (msg_param->sup_to == user_connection_param_conf.time_out))
            {
				printk("  match!\n");
            }
        } break;

        // Characteristic value read request
        case CUSTS1_VALUE_REQ_IND:
        {
			printk("CUSTS1_VALUE_REQ_IND!\n");
            struct custs1_value_req_ind const *msg_param = (struct custs1_value_req_ind const *) param;

            // Handle undefined read requests by returning an error
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

        // GATT event request indication (confirm unhandled indications to avoid a timeout)
        case GATTC_EVENT_REQ_IND:
        {
            struct gattc_event_ind const *ind = (struct gattc_event_ind const *) param;
            struct gattc_event_cfm *cfm = KE_MSG_ALLOC(GATTC_EVENT_CFM, src_id, dest_id, gattc_event_cfm);
            cfm->handle = ind->handle;
            KE_MSG_SEND(cfm);
        } break;

		// MTU (Maximum Transmission Unit) changed indication
		case GATTC_MTU_CHANGED_IND:
		{
			struct gattc_mtu_changed_ind *ind = (struct gattc_mtu_changed_ind *) param;
			printk("GATTC_MTU_CHANGED_IND: %d\n", ind->mtu);
		} break;

        // Unhandled message
        default:
		{
			printk("Unhandled msgid=%08x\n", msgid);
		} break;
    }
}

/// @} APP
