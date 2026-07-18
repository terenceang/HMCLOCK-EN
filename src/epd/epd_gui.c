

#include "epd.h"


/******************************************************************************/


// Framebuffer parameters
int fb_w;
int fb_h;

#define FB_SIZE 4736
u8 fb_bw[FB_SIZE];
u8 fb_rr[FB_SIZE];

/******************************************************************************/


void draw_pixel(int x, int y, int color)
{
	int nx, ny;
	int rmode = scr_mode&0x03;
	
	if(rmode==0){
		nx = x;
		ny = y;
	}else if(rmode==1){
		nx = scr_w-1-y;
		ny = x;
	}else if(rmode==2){
		nx = scr_w-1-x;
		ny = scr_h-1-y;
	}else if(rmode==3){
		nx = y;
		ny = scr_h-1-x;
	}
	if(scr_mode&MIRROR_H){
		nx += scr_padding;
	}

	int byte_pos = ny*line_bytes+(nx>>3);
	int bit_mask = 0x80>>(nx&7);

	if(color!=WHITE){
		fb_bw[byte_pos] &= ~bit_mask;
	}
	if(scr_mode&EPD_BWR && color==RED){
		fb_rr[byte_pos] |=  bit_mask;
	}
}


void draw_hline(int y, int x1, int x2, int color)
{
	int x;
	if(x1>x2){
		x = x1;
		x1 = x2;
		x2 = x;
	}
	
	for(x=x1; x<=x2; x+=1){
		draw_pixel(x, y, color);
	}
}


void draw_vline(int x, int y1, int y2, int color)
{
	int y;
	if(y1>y2){
		x = y1;
		y1 = y2;
		y2 = x;
	}
	
	for(y=y1; y<=y2; y+=1){
		draw_pixel(x, y, color);
	}
}


void draw_rect(int x1, int y1, int x2, int y2, int color)
{
	draw_hline(y1, x1, x2, color);
	draw_hline(y2, x1, x2, color);
	draw_vline(x1, y1, y2, color);
	draw_vline(x2, y1, y2, color);
}


void draw_box(int x1, int y1, int x2, int y2, int color)
{
	int y;
	
	for(y=y1; y<=y2; y++){
		draw_hline(y, x1, x2, color);
	}
}



/**
 * Draw a QR code to the e-paper screen
 *
 * @param start_x   drawing start X position
 * @param start_y   drawing start Y position
 * @param pix_size  width/height of each QR module (in pixels)
 * @param img       QR code C array, sized 31x4, 4 bytes per row
 */
void draw_qr_code(
    int start_x,
    int start_y,
    int pix_size,
    const unsigned char img[31][4]
) {
    for (int y = 0; y < 31; y++) {
        for (int x = 0; x < 31; x++) {

            int byte_idx = x / 8;       // 4 bytes per row, 8 bits per byte
            int bit_idx = 7 - (x % 8);  // image stored MSB-first
            int bit = (img[y][byte_idx] >> bit_idx) & 1;
            int color = (bit == 0) ? WHITE : BLACK;

            // Draw scaled up
            int x1 = start_x + x * pix_size;
            int y1 = start_y + y * pix_size;
            int x2 = x1 + pix_size - 1;
            int y2 = y1 + pix_size - 1;

            draw_box(x1, y1, x2, y2, color);
        }
    }
}

/******************************************************************************/


#include "sfont.h"
#include "sfont16.h"
#include "font50.h"
#include "font66.h"

const u8 *font_list[6] = {
	sfont,
	F_DSEG7_50,
	sfont16,
	F_DSEG7_66,
};

const u8 *current_font = (u8*)sfont;

int select_font(int id)
{
	current_font = font_list[id];
	return 0;
}


static const u8 *find_font(const u8 *font, int ucs)
{
	int total = *(u16*)font;
	int i;

	for(i=0; i<total; i++){
		if(*(u16*)(font+2+i*4+0)==ucs){
			int offset = *(u16*)(font+2+i*4+2);
			//printk("  %04x at %04x\n", ucs, offset);
			return font+offset;
		}
	}

	return NULL;
}


int fb_draw_font_info(int x, int y, const u8 *font_data, int color)
{
	int r, c;

	int ft_adv = font_data[0];
	int ft_bw = font_data[1];
	int ft_bh = font_data[2];
	int ft_bx = (signed char)font_data[3];
	int ft_by = (signed char)font_data[4];
	int ft_lsize = (ft_bw+7)/8;
	font_data += 5;

	x += ft_bx;
	y += ft_by;

	for (r=0; r<ft_bh; r++) {
		for (c=0; c<ft_bw; c++) {
			int b = font_data[c>>3];
			int mask = 0x80>>(c%8);
			if(b&mask){
				draw_pixel(x+c, y, color);
			}
			mask >>= 1;
		}
		font_data += ft_lsize;
		y += 1;
	}

	return ft_adv;
}


int fb_draw_font(int x, int y, int ucs, int color)
{
	const u8 *font_data = find_font(current_font, ucs);
	if(font_data==NULL){
		printf("fb_draw %04x: not found!\n", ucs);
		return -1;
	}

	return fb_draw_font_info(x, y, font_data, color);
}


static int utf8_to_ucs(char **ustr)
{
	u8 *str = (u8*)*ustr;
	int ucs = 0;

	if(*str==0){
		return 0;
	}else if(*str<0x80){
		*ustr = (char*)str+1;
		return *str;
	}else if(*str<0xe0){
		ucs = ((str[0]&0x1f)<<6) | (str[1]&0x3f);
		*ustr = (char*)str+2;
		return ucs;
	}else{
		ucs = ((str[0]&0x0f)<<12) | ((str[1]&0x3f)<<6) | (str[2]&0x3f);
		*ustr = (char*)str+3;
		return ucs;
	}
}


void draw_text(int x, int y, char *str, int color)
{
	int ch;

	while(1){
		ch = utf8_to_ucs(&str);
		if(ch==0)
			break;
		x += fb_draw_font(x, y, ch, color);
	}
}


// Compute the total pixel width of a string in the current font (without drawing it)
int text_width(char *str)
{
	int w = 0;
	int ch;

	while(1){
		ch = utf8_to_ucs(&str);
		if(ch==0)
			break;
		const u8 *font_data = find_font(current_font, ch);
		if(font_data)
			w += font_data[0]; // ft_adv
	}

	return w;
}


// Draw a string horizontally centered on cx
void draw_text_centered(int cx, int y, char *str, int color)
{
	int w = text_width(str);
	draw_text(cx - w/2, y, str, color);
}


/******************************************************************************/
#if 0
char *wday_str[] = {
	"Mon",
	"Tue",
	"Wed",
	"Thu",
	"Fri",
	"Sat",
	"Sun",
};


static int wday = 0;
void fb_test(void)
{
	memset(fb_bw, 0xff, scr_h*line_bytes);
	if(scr_mode&EPD_BWR){
		memset(fb_rr, 0x00, scr_h*line_bytes);
	}

	draw_rect(0, 0, fb_w-1, fb_h-1, BLACK);
	draw_rect(1, 1, fb_w-2, fb_h-2, BLACK);

#if 0
	for(int i=0; i<3; i++){
		int x = 8+i*38;
		int y = 30;
		draw_rect(x, y, x+29, y+29, BLACK);
		//draw_rect(x+1, y+1, x+29-1, y+29-1, BLACK);
		draw_box(x+2, y+2, x+29-2, y+29-2, i);
	}
#endif

	select_font(0);

	char tbuf[64];
	sprintk(tbuf, "%4d-%2d-%2d %s", 2025, 4, 29, wday_str[wday]);
	draw_text(15, 85, tbuf, BLACK);
	
	wday += 1;
	if(wday==7)
		wday = 0;

	select_font(1);
	sprintk(tbuf, "%02d:%02d", 2+wday, 30+wday);
	draw_text(12, 20, tbuf, BLACK);

	epd_screen_update();
}
#endif

/******************************************************************************/



