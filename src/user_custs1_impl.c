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
 * copyright notice and disclaimer ("Notice") is included in all such copies. Renesas
 * reserves the right to change or discontinue the Software at any time without notice.
 *
 * THE SOFTWARE IS PROVIDED "AS IS". RENESAS DISCLAIMS ALL WARRANTIES OF ANY KIND,
 * WHETHER EXPRESS, IMPLIED, OR STATUTORY, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. TO THE
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

#include "gpio.h"               // GPIO control header
#include "app_api.h"            // Application API
#include "app.h"                // Core application functionality
#include "prf_utils.h"          // BLE profile utilities
#include "custs1.h"             // Custom Server 1
#include "custs1_task.h"        // Custom Server 1 task
#include "user_custs1_def.h"    // Custom Server 1 definitions
#include "user_custs1_impl.h"   // Custom Server 1 implementation
#include "user_peripheral.h"    // User peripheral related
#include "user_periph_setup.h"  // User peripheral setup
#include "adc.h"                // ADC (analog-to-digital converter) related

#include "epd.h"                // E-paper display driver

/*
 * GLOBAL VARIABLE DEFINITIONS
 * These variables use the __SECTION_ZERO("retention_mem_area0") attribute to
 * place them in the power-loss-retained memory area
 ****************************************************************************************
 */

// Timer ID, used for scheduled system tasks
ke_msg_id_t timer_used      __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// Indication counter, used for BLE notification counting
uint16_t indication_counter __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// Non-database value counter
uint16_t non_db_val_counter __SECTION_ZERO("retention_mem_area0"); //@RETENTION MEMORY
// ADC sample value, used for battery level detection
int adcval;

static uint8_t h24_format = 1; // 24-hour format flag

extern int adv_state;
/*
 * FUNCTION DEFINITIONS
 ****************************************************************************************
 */


/**
 * @brief Update the ADC sample value and send the battery voltage over BLE
 *
 * This function performs the following:
 * 1. Calibrate the ADC offset
 * 2. Sample the battery voltage
 * 3. Convert the sample to an actual voltage value
 * 4. Send the voltage value to the connected device over BLE
 *
 * @return The computed voltage value
 */
int adc1_update(void)
{
    // Calibrate the ADC offset, using single-ended input mode
    adc_offset_calibrate(ADC_INPUT_MODE_SINGLE_ENDED);
    // Sample the battery voltage
    adcval = adc_get_vbat_sample(false);
    // Convert the ADC value to an actual voltage value (units: mV)
    int volt = (adcval*225)>>7;

    // Allocate memory and build the BLE message
    struct custs1_val_set_req *req = KE_MSG_ALLOC_DYN(CUSTS1_VAL_SET_REQ,
                                                      prf_get_task_from_id(TASK_ID_CUSTS1),
                                                      TASK_APP,
                                                      custs1_val_set_req,
                                                      DEF_SVC1_ADC_VAL_1_CHAR_LEN);
    // Set the connection index
    req->conidx = app_env->conidx;
    // Set the characteristic value handle
    req->handle = SVC1_IDX_ADC_VAL_1_VAL;
    // Set the data length
    req->length = DEF_SVC1_ADC_VAL_1_CHAR_LEN;
    // Set the voltage value (16-bit, low byte first)
    req->value[0] = volt&0xff;
    req->value[1] = volt>>8;
    // Send the BLE message
    KE_MSG_SEND(req);

    return volt;
}


/****************************************************************************************/

/**
 * Global time variable definitions
 * year: the year, e.g. 2025
 * month: month, 0-11 represents Jan-Dec
 * date: day of month, 0-30 represents 1st-31st
 * wday: weekday, 0-6 represents Sunday-Saturday
 * hour: hour, 0-23
 * minute: minute, 0-59
 * second: second, 0-59
 */
int year=2025, month=0, date=0, wday=3;
int hour=0, minute=0, second=0;
// Minutes elapsed since the last time sync
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


// Advance by 1 day
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

// 0: no change
// 1: minute changed
// 2: minute changed, on a 10-minute boundary
// 3: hour changed
// 4: day changed

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
 * Draw the battery level icon
 *
 * @param x top-left X coordinate of the icon
 * @param y center Y coordinate of the icon
 *
 * Icon details:
 * - Outer frame size: 16x8 pixels
 * - Level display: fills the interior based on the actual battery percentage
 * - Battery positive terminal: 2x2 pixels
 */
static void draw_batt(int x, int y)
{
    // Get the battery percentage and convert it to a fill-segment count (0-10)
    int p = batt_cal(adcval);
    p /= 10;

    // Draw the battery outer frame
    draw_rect(x, y-4, x+14, y+4, BLACK);
    // Draw the battery positive terminal
    draw_box(x-2, y-1, x-1, y+1, BLACK);

    // Draw the fill portion representing the level
    draw_box(x+12-p, y-2, x+12, y+2, BLACK);
}


const u8 font_bt[] = {
	0x08, 0x08, 0x0f, 0x00, 0x00,
	0x10, 0x18, 0x14, 0x92, 0x51, 0x32, 0x14, 0x18,
	0x14, 0x32, 0x51, 0x92, 0x14, 0x18, 0x10,
};

/**
 * Draw the Bluetooth icon
 *
 * @param x center X coordinate of the icon
 * @param y center Y coordinate of the icon
 *
 * Icon details: an 8x15 glyph
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

// slot 0: date (top, only y is used, drawn horizontally centered)
// slot 1: Bluetooth icon (device ID follows right next to it, shown only while advertising)
// slot 2: battery icon
// slot 3: time (middle, only y is used, drawn horizontally centered)
// slot 4: unused (formerly the lunar date, now removed)
// slot 5: unused (formerly the solar term, now removed)
// slot 6: weekday (bottom, only y is used, drawn horizontally centered)
// slot 7: AM/PM (positioned dynamically right after the time, only this y is used)

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
 * E-paper screen update wait timer
 *
 * Behavior:
 * - Checks whether the e-paper screen is busy
 * - If busy, checks again after 40ms
 * - If idle, finishes the update sequence and enters power-saving mode
 *
 * Once the e-paper update completes:
 * 1. Send the deep sleep command (0x10, 0x01)
 * 2. Power off
 * 3. Close the hardware interface
 * 4. Put the system into extended sleep mode
 */
static void epd_wait_timer(void)
{
    if(epd_busy()){
        // Screen is still busy, check again in 40ms
        epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
    }else{
        // Screen update complete
        epd_wait_hnd = EASY_TIMER_INVALID_TIMER;
        // Send the deep sleep command
        epd_cmd1(0x10, 0x01);
        // Power off
        epd_power(0);
        // Close the hardware interface
        epd_hw_close();
        // Put the system into extended sleep mode
        arch_set_sleep_mode(ARCH_EXT_SLEEP_ON);
    }
}


void QR_draw()
{
	char tbuf[16];

	// QR code drawing logic goes here
	epd_hw_open();

	epd_update_mode(UPDATE_FULL);

	memset(fb_bw, 0xff, scr_h*line_bytes);
	memset(fb_rr, 0x00, scr_h*line_bytes);

	draw_qr_code(5, 5, 3, QR_31x31);
	draw_text(100, 5,"Bluetooth", BLACK);
	// Draw the device name as a single contiguous string (DCLK-XXYYZZ) in one
	// call, rather than splitting it across two draw_text calls at fixed x
	// positions, which produced a visual gap.
	sprintf(tbuf, "DCLK-%s", bt_id);
	draw_text(100, 20, tbuf, BLACK);

	draw_text(110,40,"-------------",BLACK);

	draw_text(100, 60, "Scan the QR code", BLACK);
	draw_text(100, 75, "with your browser", BLACK);
	// Update the e-paper display
	epd_init();
	epd_screen_update();
	epd_update();
	// Deep sleep during an update causes screen corruption. Temporarily disable sleep.
	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
}

void LB_draw()
{
	// Low-battery icon drawing logic goes here
	epd_hw_open();

	epd_update_mode(UPDATE_FULL);

	memset(fb_bw, 0xff, scr_h*line_bytes);
	memset(fb_rr, 0x00, scr_h*line_bytes);

	draw_qr_code(60, 10, 4, LB_31x31);

	// Update the e-paper display
	epd_init();
	epd_screen_update();
	epd_update();
	// Deep sleep during an update causes screen corruption. Temporarily disable sleep.
	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
}

/**
 * Draw the clock screen
 *
 * @param flags display control flags
 *              bit0-1: update mode (fast/normal)
 *              bit2: whether to show the Bluetooth icon
 *
 * Displayed content includes:
 * - Battery level icon
 * - Bluetooth connection icon (optional)
 * - Large-digit time display
 * - Date and weekday (English)
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

	int cx = lt->xres/2;

	// Draw the battery level
	draw_batt(lt->x[2], lt->y[2]);
	if(flags&DRAW_BT){
		// Draw the Bluetooth icon
		draw_bt(lt->x[1], lt->y[1]);
	}

	// Top, center-drawn date (DD-MM-YYYY)
	sprintf(tbuf, "%02d-%02d-%04d", date+1, month+1, year);
	select_font(lt->font_char);
	draw_text_centered(cx, lt->y[0], tbuf, BLACK);

	// Middle, center-drawn time (large digits)
	select_font(lt->font_dseg);
	if(h24_format){
		// 24-hour format
		sprintf(tbuf, "%02d:%02d", hour, minute);
	}else{
		// 12-hour format
		int h = hour;
		int ampm = 0;
		if(h>=12){
			if(h>12)
				h -= 12;
			ampm = 1;
		}else if(h==0){
			h = 12; // midnight is displayed as 12
		}
		sprintf(tbuf, "%2d:%02d", h, minute);
	}
	{
		int tw = text_width(tbuf);
		int tx = cx - tw/2;
		draw_text(tx, lt->y[3], tbuf, BLACK);

		if(!h24_format){
			// AM/PM immediately follows the time on its right
			int h = hour;
			int ampm = (h>=12);
			select_font(lt->font_char);
			draw_text(tx+tw+4, lt->y[7], ampm? "PM":"AM", BLACK);
		}
	}

	// Bottom, center-drawn weekday (device ID suffix appended while advertising)
	select_font(lt->font_char);
	if(flags&DRAW_BT){
		sprintf(tbuf, "%s  %s", wday_str[wday], bt_id);
		draw_text_centered(cx, lt->y[6], tbuf, BLACK);
	}else{
		draw_text_centered(cx, lt->y[6], wday_str[wday], BLACK);
	}

	// Update the e-paper display
	epd_init();
	epd_screen_update();
	epd_update();
	// Deep sleep during an update causes screen corruption. Temporarily disable sleep.
	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	epd_wait_hnd = app_easy_timer(40, epd_wait_timer);
}


/****************************************************************************************/


/**
 * Control point write indication handler
 *
 * @param msgid message ID
 * @param param write parameters
 * @param dest_id destination task ID
 * @param src_id source task ID
 *
 * Handles control commands received over BLE
 */
void user_svc1_ctrl_wr_ind_handler(ke_msg_id_t const msgid,
                                  struct custs1_val_write_ind const *param,
                                  ke_task_id_t const dest_id,
                                  ke_task_id_t const src_id)
{
    // Print the received control command
    printk("Control Point: %02x\n", param->value[0]);
}

/**
 * Long value characteristic write indication handler
 *
 * @param msgid message ID
 * @param param write parameters
 * @param dest_id destination task ID
 * @param src_id source task ID
 *
 * Handles commands:
 * - 0x91: clock-set command
 * - 0xA0 and above: OTA update related commands
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
		// Set the clock (needs at least buf[1..8], 9 bytes total)
		if(len<9) return;
		clock_set((uint8_t*)param->value);
		// Update the display (with Bluetooth icon, fast update mode)
		clock_draw(DRAW_BT|UPDATE_FAST);
		// Print the current time
		clock_print();
	}else if(param->value[0]==0x90){
		// Toggle 24/12-hour format
		h24_format = !h24_format;
		clock_draw(DRAW_BT|UPDATE_FAST);
	}else if(param->value[0]==0x92){
		// Time calibration (needs at least buf[1..2], 3 bytes total)
		if(len<3) return;
		int diff_sec;
		diff_sec  = param->value[1];
		diff_sec |= param->value[2]<<8;
		diff_sec  = (diff_sec<<16)>>16;
		printk("Calibration: %02x\n", diff_sec);
		clock_fixup_set(diff_sec, cal_minute);
		cal_minute = 0;
	}else if(param->value[0]>=0xa0){
		// Handle OTA update commands
		ota_handle((u8*)param->value, len);
    }
}

/**
 * Long value characteristic attribute info request handler
 *
 * @param msgid message ID
 * @param param request parameters
 * @param dest_id destination task ID
 * @param src_id source task ID
 *
 * Responds to a BLE client's attribute info request
 */
void user_svc1_long_val_att_info_req_handler(ke_msg_id_t const msgid,
                                            struct custs1_att_info_req const *param,
                                            ke_task_id_t const dest_id,
                                            ke_task_id_t const src_id)
{
    // Allocate the response message
    struct custs1_att_info_rsp *rsp = KE_MSG_ALLOC(CUSTS1_ATT_INFO_RSP,
                                                   src_id,
                                                   dest_id,
                                                   custs1_att_info_rsp);
    // Set the connection index
    rsp->conidx  = app_env[param->conidx].conidx;
    // Set the attribute index
    rsp->att_idx = param->att_idx;
    // Set the length to 0
    rsp->length  = 0;
    // Set the status to no error
    rsp->status  = ATT_ERR_NO_ERROR;

    // Send the response message
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
