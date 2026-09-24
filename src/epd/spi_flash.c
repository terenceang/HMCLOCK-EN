
#include "epd.h"




static int spio_clk;
static int spio_cs;
static int spio_di;
static int spio_do;

/******************************************************************************/


#define FSPI_CS(n)  gpio_set(spio_cs,  (n));
#define FSPI_CK(n)  gpio_set(spio_clk, (n));
#define FSPI_SI(n)  gpio_set(spio_di,  (n));
#define FSPI_SO()   gpio_get(spio_do);

#define FSPI_DELAY     10

void fspi_delay(void)
{
	int i;

	for(i=0; i<FSPI_DELAY; i++){
		__asm("nop");
	}
}


int fspi_trans(int byte)
{
	int i, data;

	data = 0;
	for(i=0; i<8; i++){
		FSPI_SI(byte&0x80);
		FSPI_CK(0);
		fspi_delay();

		data <<= 1;
		data |= FSPI_SO();

		FSPI_CK(1);
		fspi_delay();
		byte <<= 1;
	}

	return data;
}


int fspi_config(u32 gpio_word)
{
	spio_clk = (gpio_word>>24)&0xff;
	spio_cs  = (gpio_word>>16)&0xff;
	spio_di  = (gpio_word>> 8)&0xff;
	spio_do  = (gpio_word>> 0)&0xff;

	return 0;
}


int fspi_init(void)
{
	gpio_config(spio_clk, 0x0300, 0);
	gpio_config(spio_cs,  0x0300, 1);
	gpio_config(spio_di,  0x0300, 1);
	gpio_config(spio_do,  0x0100, 1);

	FSPI_CS(0);
	fspi_delay();
	fspi_trans(0xab);
	FSPI_CS(1);

	return 0;
}


int fspi_exit(void)
{
	gpio_config(spio_cs,  0x0300, 1);
	gpio_config(spio_clk, 0x0300, 0);
	gpio_config(spio_do,  0x0300, 0);
	gpio_config(spio_di,  0x0100, 0);
	return 0;
}


/******************************************************************************/
/* SPI flash                                                                  */
/******************************************************************************/ 

int sf_readid(void)
{
	FSPI_CS(0);
	fspi_delay();

	fspi_trans(0x90);
	fspi_trans(0x00);
	fspi_trans(0x00);
	fspi_trans(0x00);

	int mid = fspi_trans(0);
	int pid = fspi_trans(0);
	FSPI_CS(1);
	fspi_delay();

	return (mid<<8)|pid;
}

int sf_status(int id)
{
	int status;
	int cmd = (id)? 0x35 : 0x05;

	FSPI_CS(0);
	fspi_delay();
	fspi_trans(cmd);
	status = fspi_trans(0);
	FSPI_CS(1);
	fspi_delay();

	return status&0xff;
}

int sf_wstat(int id, int stat)
{
	int cmd = (id)? 0x31 : 0x01;

	// status write enable
	FSPI_CS(0);
	fspi_delay();
	fspi_trans(0x50);
	FSPI_CS(1);
	fspi_delay();

	FSPI_CS(0);
	fspi_delay();
	fspi_trans(cmd);
	fspi_trans(stat);
	FSPI_CS(1);
	fspi_delay();

	return 0;
}

int sf_wen(int en)
{
	FSPI_CS(0);
	fspi_delay();
	if(en)
		fspi_trans(0x06);
	else
		fspi_trans(0x04);
	FSPI_CS(1);
	fspi_delay();

	return 0;
}

int sf_wait(void)
{
	int status;

	while(1){
		status = sf_status(0);
		if((status&1)==0)
			break;
		fspi_delay();
	}

	status |= sf_status(1)<<8;
	return status;
}


int sf_sector_erase(int cmd, int addr, int wait)
{
	sf_wen(1);

	FSPI_CS(0);
	fspi_delay();
	fspi_trans(cmd);
	fspi_trans((addr>>16)&0xff);
	fspi_trans((addr>> 8)&0xff);
	fspi_trans((addr>> 0)&0xff);
	FSPI_CS(1);
	fspi_delay();

	if(wait){
		int status = sf_wait();
		return status;
	}else{
		return 0;
	}
}

int sf_erase(int addr, int size, int wait)
{
	int status;

	while(size>0){
		if((addr%0x8000)==0 && size>=0x8000){
			status = sf_sector_erase(ERASE_32K, addr, wait);
			printk("sf_erase: %08x 32K  stat=%04x\n", addr, status);
			addr += 0x8000;
			size -= 0x8000;
		}else
		{
			status = sf_sector_erase(ERASE_4K,  addr, wait);
			printk("sf_erase: %08x  4K  stat=%04x\n", addr, status);
			addr += 0x1000;
			size -= 0x1000;
		}
	}

	return 0;
}


int sf_page_write(int addr, u8 *buf, int size)
{
	int i;

	sf_wen(1);

	FSPI_CS(0);
	fspi_delay();

	fspi_trans(0x02);
	fspi_trans((addr>>16)&0xff);
	fspi_trans((addr>> 8)&0xff);
	fspi_trans((addr>> 0)&0xff);

	for(i=0; i<size; i++){
		fspi_trans(buf[i]);
	}

	FSPI_CS(1);
	fspi_delay();
	return 0;
}


int sf_read(int addr, int len, u8 *buf)
{
	int i;

	FSPI_CS(0);
	fspi_delay();

	fspi_trans(0x0b);
	fspi_trans((addr>>16)&0xff);
	fspi_trans((addr>> 8)&0xff);
	fspi_trans((addr>> 0)&0xff);

	fspi_trans(0);
	for(i=0; i<len; i++){
		buf[i] = fspi_trans(0);
	}

	FSPI_CS(1);
	fspi_delay();

	return len;
}

/******************************************************************************/


static const uint32_t crc32_tab[] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

uint32_t crc32(uint32_t crc, const void *buf, size_t size)
{
    const uint8_t *p;

    p = buf;
    crc = crc ^ ~0U;

    while (size--)
        crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

    return crc ^ ~0U;
}


/******************************************************************************/

extern int Region$$Table$$Base;
void sf_dumpp(int addr, int size);

// Pick the slot the booter will boot: the one with the higher generation
// flag. The flag byte in the image header (offset 3) is interpreted with
// signed-char semantics by both this code and the OTP booter.
static int pick_active_image(int flag0, int flag1)
{
	return (flag0 >= flag1) ? 0 : 1;
}

// Compute the generation flag for a new image from the active image's flag.
// A naive +1 overflows at 0x7f: 0x7f+1 == 0x80 == -128 as a signed char,
// which ranks BELOW every other flag, so the booter would silently revert to
// the old image on every reset (and re-flashing recomputes 0x80 forever).
// Repair: once the active flag reaches 0x7f, pull the old header's flag byte
// back to 0x01. NOR programming can only clear bits, so overwriting the byte
// with 0x01 needs no erase. The new image then gets 0x02 and outranks it.
// A negative (already-wrapped) active flag yields a non-positive new flag;
// clamp that to 1 so the fresh image still outranks the wrapped old one.
static int bump_image_flag(int active_flag, int active_hdr_addr)
{
	int nf = active_flag + 1;

	if(nf > 0x7e || nf < 1){
		if(active_flag >= 0x7f){
			u8 low = 0x01;
			sf_page_write(active_hdr_addr+3, &low, 1);
			sf_wait();
		}
		nf = (nf > 0x7e) ? 2 : 1;
	}

	return nf;
}

// Boot/flash diagnostics, filled in by selflash() and exposed via the
// readable FF04 characteristic (see diag_val_update() in user_custs1_impl.c)
// so they can be inspected with any BLE scanner without a UART console.
// Layout (little-endian):
// [0..3]   otp_boot marker arg passed to selflash()
// [4..19]  boot header @0x00000 (raw 16 bytes; describes the image the
//          ROM/OTP booter loads when it ignores the A/B product scheme)
// Single-image (AN-B-001) boot path:
// [20..23] image length field from the boot header
// [24..27] CRC32 of the flash image at offset 8
// [28..31] size of the running image
// A/B (product header) boot path:
// [20..23] image0 version field (header offset 28)
// [24..27] image1 version field (header offset 28)
// [28..29] image0 address (low 16 bits)
// [30..31] image1 address (low 16 bits)
volatile u8 flash_diag[32];

static void flash_diag_set(int off, u32 v)
{
	flash_diag[off+0] = v & 0xff;
	flash_diag[off+1] = (v>>8) & 0xff;
	flash_diag[off+2] = (v>>16) & 0xff;
	flash_diag[off+3] = (v>>24) & 0xff;
}

// Program 'len' bytes from RAM 'src' to flash 'dst'. Writes are split so a
// page never straddles a flash 256-byte page boundary -- NOR page programs
// wrap around within the page, which would silently corrupt the tail.
static void sf_write_mem(int dst, u8 *src, int len)
{
	u8 pbuf[256];

	while(len>0){
		int n = 256 - (dst & 0xff);
		if(n>len) n = len;
		memcpy(pbuf, src, n);
		sf_page_write(dst, pbuf, n);
		sf_wait();
		dst += n; src += n; len -= n;
	}
}

// Install the running image (RAM at 0x07fc0000) into the primary image slot
// (product header slot 0) -- the slot the boot chain actually boots on these
// units. Header uses the Dialog SUOTA layout: 70 51 AA <imageid>, code_size,
// CRC, version @28, encryption @32 = 0. The generation id is set one above
// the highest existing slot id (wrap-safe), so this image wins on booters
// that pick slot 0 unconditionally *and* on booters that pick the highest id.
static void selflash_install(int firm_size, u32 firm_crc, int slot0, int slot1)
{
	u8 pbuf[256];
	u32 *p32 = (u32*)pbuf;
	int f0 = -1, f1 = -1, hi, new_flag;

	// Diag: [20..22] = running version (low 3 bytes), [23] = what this boot
	// did: 0x01 = slot install ran, 0x02 = up-to-date skip; when reinstalling,
	// bits 04/08/10/20 flag which field(s) mismatched (magic/size/crc/version).
	flash_diag[20] = EPD_VERSION & 0xff;
	flash_diag[21] = (EPD_VERSION>>8) & 0xff;
	flash_diag[22] = (EPD_VERSION>>16) & 0xff;
	flash_diag[23] = 0x00;
	flash_diag_set(24, firm_crc);
	flash_diag_set(28, firm_size);

	// Up to date? Slot 0 already carries this exact build. While checking,
	// record which field(s) mismatched into the diag status byte (bits 2..5)
	// so a reinstall-every-boot problem can be diagnosed over BLE.
	sf_read(slot0, 64, pbuf);
	if(pbuf[0]!=0x70 || pbuf[1]!=0x51) flash_diag[23] |= 0x04;
	if(p32[1]!=(u32)firm_size)         flash_diag[23] |= 0x08;
	if(p32[2]!=firm_crc)               flash_diag[23] |= 0x10;
	if(p32[7]!=EPD_VERSION)            flash_diag[23] |= 0x20;
	if((flash_diag[23] & 0x3c)==0){
		printk("Slots up to date.\n");
		flash_diag[23] |= 0x02;
		return;
	}

	flash_diag[23] |= 0x01;

	printk("Install image to slot %08x (size %d)\n", slot0, firm_size);

	// Generation id: one above the highest existing slot id, wrap-safe.
	sf_read(slot0, 64, pbuf);
	if(pbuf[0]==0x70 && pbuf[1]==0x51) f0 = (signed char)pbuf[3];
	sf_read(slot1, 64, pbuf);
	if(pbuf[0]==0x70 && pbuf[1]==0x51) f1 = (signed char)pbuf[3];
	hi = (f0 >= f1) ? f0 : f1;
	new_flag = bump_image_flag(hi, (f0 >= f1) ? slot0 : slot1);

	// Erase and write only the primary slot's own region.
	sf_erase(slot0, 64+firm_size, 1);

	memset(pbuf, 0xff, 64);
	pbuf[0] = 0x70;
	pbuf[1] = 0x51;
	pbuf[2] = 0xaa;
	pbuf[3] = new_flag;
	p32[1] = firm_size;
	p32[2] = firm_crc;
	p32[7] = EPD_VERSION;
	pbuf[0x20] = 0;
	sf_write_mem(slot0, pbuf, 64);

	sf_write_mem(slot0+64, (u8*)0x07fc0000, firm_size);

	printk("Slot install done.\n");
}

int selflash(int otp_boot)
{
	u8 pbuf[256];
	u32 *p32 = (u32*)pbuf;
	int image_addr[2];

	fspi_init();
	int id = sf_readid();
	printk("Flash  ID: %08x\n", id);

	for(int i=0; i<4; i++){
		flash_diag[i] = ((u8*)&otp_boot)[i];
	}
	sf_read(0x00000, 16, (u8*)flash_diag+4);

	sf_read(0x39000, 16, pbuf);
	printk("39000: ");
	for(int i=0; i<16; i++){
		printk(" %02x", pbuf[i]);
	}
	printk("\n");
	printk("EPD Type: %02x\n", pbuf[0]);
	if(pbuf[1]==0x01){
		printk("EPD Gpio: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			pbuf[8], pbuf[9], pbuf[10], pbuf[11], pbuf[12], pbuf[13], pbuf[14], pbuf[15]);
		// pbuf[8..15] = CS, ??, RST, CLK, SDI, DC, BUSY, PWR (each already
		// encoded as the group<<4|pin byte epd_hw_init expects), so the
		// panel's real wiring can be used instead of guessing between the
		// hardcoded pinouts in user_app_init().
		detect_config0 = (pbuf[15]<<24) | (pbuf[14]<<16) | (pbuf[10]<<8);
		detect_config1 = (pbuf[13]<<24) | (pbuf[ 8]<<16) | (pbuf[11]<<8) | pbuf[12];
		printk("EPD Pinout: %08x %08x\n", detect_config0, detect_config1);
	}

	sf_read(0x3a000, 16, pbuf);
	printk("3a000: ");
	for(int i=0; i<16; i++){
		printk(" %02x", pbuf[i]);
	}
	printk("\n");

	int xres = *(u16*)(pbuf+10);
	int yres = *(u16*)(pbuf+12);
	if(xres<512 && yres<512){
		detect_w = xres;
		detect_h = yres;
		detect_mode = pbuf[9]? EPD_BWR : EPD_BW;
		printk("EPD  Res: %dx%d  %d\n", xres, yres, pbuf[9]);
	}

	int region_table = (int)&Region$$Table$$Base;
	int firm_size = *(u32*)(region_table+0x10) - 0x07fc0000;
	printk("Firm size: %08x\n", firm_size);
	u32 firm_crc = crc32(0, (u8*)0x07fc0000, firm_size);
	printk("Firm  crc: %08x\n", firm_crc);
	printk("Firm  ver: %08x\n", EPD_VERSION);


	memset(pbuf, 0, 256);
	if(otp_boot==0x1234a5a5){
		// Booted via the OTP/ROM boot chain. Read the product header to find
		// the image slots, then install the running firmware into the primary
		// slot with a generation id that outranks both existing slots.
		sf_read(0x38000, 16, pbuf);
		if(pbuf[0]!=0x70 || pbuf[1]!=0x52){
			printk("Build Product header ...\n");
			p32[0] = 0x00005270;
			p32[1] = 0x00004000;
			p32[2] = 0x0001f000;
			sf_sector_erase(ERASE_4K, 0x38000, 1);
			sf_page_write(0x38000, pbuf, 12);
			sf_wait();
		}
		image_addr[0] = p32[1];
		image_addr[1] = p32[2];
		printk("Slots: %08x + %08x\n", image_addr[0], image_addr[1]);

		selflash_install(firm_size, firm_crc, image_addr[0], image_addr[1]);

		fspi_exit();
		return 0;

	}else{
		// Booted from Flash. Read the boot header.
		sf_read(0, 16, pbuf);
		printk("Boot Header: %08x %08x\n", *(u32*)(pbuf+0), *(u32*)(pbuf+4));

		// Best-effort diagnostics: expose the A/B image versions and slot
		// addresses (product header if valid, defaults otherwise).
		sf_read(0x38000, 12, pbuf);
		if(pbuf[0]==0x70 && pbuf[1]==0x52){
			image_addr[0] = p32[1];
			image_addr[1] = p32[2];
		}else{
			image_addr[0] = 0x04000;
			image_addr[1] = 0x1f000;
		}
		sf_read(image_addr[0]+28, 4, (u8*)flash_diag+20);
		sf_read(image_addr[1]+28, 4, (u8*)flash_diag+24);
		flash_diag_set(28, image_addr[0]);
		flash_diag_set(30, image_addr[1]&0xffff);
	}

	fspi_exit();
	return 0;
}

/******************************************************************************/


int ota_state = 0;
static u8  ota_buf[256];
static int firm_addr;
static int firm_size;
static int firm_flag;

// Running CRC32 over the OTA payload as it's received, checked against the
// CRC the client embedded in the image header (page 0, offset 8) before we
// ever reset into the new image. ota_crc_len tracks how many *real* firmware
// bytes (excluding the 64-byte header prologue and any trailing 0xff pad on
// the last page) have been folded into ota_crc so far.
static u32 ota_crc;
static u32 ota_crc_expect;
static int ota_crc_len;
static int ota_crc_valid;


int ota_handle(u8 *buf, int len)
{
	u8 *pbuf = ota_buf;
	u32 *p32 = (u32*)pbuf;
	int image_addr[2];
	int image_flag[2];

	if(buf[0]==0xa0){
		// Update starting
		// Erase the inactive firmware slot first
		if(len<4) return -1;
		ota_state = 1;
		ota_crc = 0;
		ota_crc_len = 0;
		ota_crc_valid = 0;

		firm_size = *(u16*)(buf+2);
		printk("firm_size: %04x (%d)\n", firm_size, firm_size);

		fspi_init();
		int id = sf_readid();
		printk("flash id: %04x\n", id);

		sf_read(0x38000, 16, pbuf);
		image_addr[0] = p32[1];
		image_addr[1] = p32[2];
		printk("image0: %08x   image1: %08x\n", image_addr[0], image_addr[1]);

		// Read the image headers
		sf_read(image_addr[0], 32, pbuf+0 );
		sf_read(image_addr[1], 32, pbuf+32);

		// Determine the id of the currently active image
		image_flag[0] = -1;
		image_flag[1] = -1;
		if(pbuf[ 0]==0x70 && pbuf[ 1]==0x51 && pbuf[ 2]==0xaa){
			image_flag[0] = (signed char)pbuf[ 3];
		}
		if(pbuf[32]==0x70 && pbuf[33]==0x51 && pbuf[34]==0xaa){
			image_flag[1] = (signed char)pbuf[35];
		}
		int active = pick_active_image(image_flag[0], image_flag[1]);
		firm_flag = bump_image_flag(image_flag[active], image_addr[active]);
		int new_id = active^1;

		firm_addr = image_addr[new_id];
		// Erase flash
		printk("Erase %08x - %08x ...\n", firm_addr, firm_addr+firm_size+64);
		sf_erase(firm_addr, firm_size+64, 1);

		arch_set_sleep_mode(ARCH_SLEEP_OFF);
	}else if(buf[0]==0xa2){
		// Transfer the first 128 bytes of the page
		if(len<136) return -1;
		memcpy(ota_buf, buf+8, 128);
	}else if(buf[0]==0xa3){
		// Transfer the last 128 bytes of the page
		if(len<136) return -1;
		memcpy(ota_buf+128, buf+8, 128);

		// Page 0 carries a 64-byte header prologue (magic/size/crc/version)
		// before the real firmware bytes start; later pages are pure data.
		int page_off = (ota_state==1) ? 64 : 0;
		int page_cap = 256 - page_off;
		int avail = firm_size - ota_crc_len;
		if(avail>page_cap) avail = page_cap;
		if(avail<0) avail = 0;

		if(ota_state==1){
			ota_buf[3] = firm_flag;
			ota_crc_expect = *(u32*)(ota_buf+8);
			ota_crc_valid = 1;
			printk("Firm Header: %08x %08x %08x %08x\n",
				*(u32*)(ota_buf+0),
				*(u32*)(ota_buf+4),
				*(u32*)(ota_buf+8),
				*(u32*)(ota_buf+28)
			);
		}
		if(avail>0){
			ota_crc = crc32(ota_crc, ota_buf+page_off, avail);
			ota_crc_len += avail;
		}

		int addr = firm_addr+(ota_state-1)*256;
		sf_page_write(addr, ota_buf, 256);
		int status = sf_wait();

		ota_state += 1;
	}else if(buf[0]==0xa4){
		// Only reset into the new image if the payload we actually wrote
		// matches the CRC the client embedded in the header. This is an
		// unauthenticated BLE-writable path, so a truncated/corrupted
		// transfer must not be allowed to become the boot image.
		int crc_ok = ota_crc_valid && (ota_crc_len==firm_size) && (ota_crc==ota_crc_expect);

		if(!crc_ok){
			printk("OTA CRC check FAILED (got %08x/%d bytes, expected %08x/%d bytes) - aborting update\n",
				ota_crc, ota_crc_len, ota_crc_expect, firm_size);
			// Invalidate the new image's header magic so a stray reset
			// (watchdog, power blip) can't later boot into it based on
			// its already-written generation flag -- writing zeros over
			// already-programmed flash needs no re-erase.
			u8 zero[4] = {0, 0, 0, 0};
			sf_page_write(firm_addr, zero, 4);
			sf_wait();
		}

		ota_state = 0;
		ota_crc_valid = 0;
		arch_set_sleep_mode(ARCH_EXT_SLEEP_ON);

		if(crc_ok){
			// Remap addres 0x00 to ROM and force execution
			SetWord16(SYS_CTRL_REG, (GetWord16(SYS_CTRL_REG) & ~REMAP_ADR0) | SW_RESET );
		}
	}

	return 0;
}


/******************************************************************************/


/******************************************************************************/
/* Uploaded image (image display mode)                                        */
/******************************************************************************/

// Layout of the 0x3b000-0x3efff region (past the product header 0x38000,
// pinout 0x39000 and panel info 0x3a000; the flash is >= 256KB so it can't wrap):
//   0x3b000  image header (16 bytes, written last -> image valid)
//   0x3c000  display-mode record {0xa5, mode}, its own sector so a mode
//            change never touches the image
//   0x3d000  pixels: logical row-major 1bpp, MSB first, 1 = white,
//            rows padded to whole bytes (drawn through draw_pixel, so the
//            web app needs no knowledge of the panel rotation)
#define IMG_HDR    0x3b000
#define IMG_MODE   0x3c000
#define IMG_DATA   0x3d000
#define IMG_MAGIC  0x31474d49	// "IMG1"
#define IMG_CHUNK  128

typedef struct {
	u32 magic;
	u16 xres, yres;
	u16 len;
	u16 pad;
	u32 crc;
} img_hdr_t;

static int img_active;
static int img_x, img_y, img_len, img_recv;
static u32 img_crc;

static int img_stride(int xres) { return (xres+7)>>3; }

// Abandon a half-received upload (disconnect); the header is already erased,
// so the old image stays invalid and the previous mode record is untouched.
void img_abort(void)
{
	if(img_active){
		img_active = 0;
		arch_set_sleep_mode(ARCH_EXT_SLEEP_ON);
	}
}

// Start an upload of an xres*yres bitmap: invalidate + erase the old image
int img_begin(int xres, int yres)
{
	img_len = img_stride(xres)*yres;
	if(img_len<=0 || img_len>0x2000) return -1;
	img_x = xres;
	img_y = yres;
	img_recv = 0;
	img_crc = 0;

	arch_set_sleep_mode(ARCH_SLEEP_OFF);
	fspi_init();
	sf_sector_erase(ERASE_4K, IMG_HDR,        1);
	sf_sector_erase(ERASE_4K, IMG_DATA,       1);
	sf_sector_erase(ERASE_4K, IMG_DATA+0x1000, 1);
	fspi_exit();
	img_active = 1;
	return 0;
}

// Chunk n (<=128 bytes) at offset seq*128; must arrive in order
int img_chunk(int seq, u8 *d, int n)
{
	if(!img_active || n<1 || n>IMG_CHUNK || seq*IMG_CHUNK!=img_recv || img_recv+n>img_len){
		return -1;
	}
	fspi_init();
	sf_page_write(IMG_DATA+img_recv, d, n);
	sf_wait();
	fspi_exit();
	img_crc = crc32(img_crc, d, n);
	img_recv += n;
	return 0;
}

// Finish: verify length + CRC (received, then re-read from flash) and only
// then write the header. Returns 0 on success.
int img_end(u32 crc)
{
	img_hdr_t h;
	u8 buf[IMG_CHUNK];
	u32 fcrc = 0;
	int ok = img_active && img_recv==img_len && img_crc==crc;

	if(ok){
		fspi_init();
		for(int off=0; off<img_len; off+=IMG_CHUNK){
			int n = img_len-off<IMG_CHUNK ? img_len-off : IMG_CHUNK;
			sf_read(IMG_DATA+off, n, buf);
			fcrc = crc32(fcrc, buf, n);
		}
		ok = fcrc==crc;
		if(ok){
			h.magic = IMG_MAGIC;
			h.xres = img_x;
			h.yres = img_y;
			h.len = img_len;
			h.pad = 0;
			h.crc = crc;
			sf_page_write(IMG_HDR, (u8*)&h, sizeof(h));
			sf_wait();
		}
		fspi_exit();
	}
	img_abort();
	return ok ? 0 : -1;
}

// Is there a valid stored image of exactly xres*yres?
static int img_valid(int xres, int yres, img_hdr_t *h)
{
	sf_read(IMG_HDR, sizeof(*h), (u8*)h);
	return h->magic==IMG_MAGIC && h->xres==xres && h->yres==yres
		&& h->len==img_stride(xres)*yres;
}

// Draw the stored image into the framebuffer (caller clears it first).
// Returns 0 on success, -1 if no valid image is stored.
int img_render(int xres, int yres)
{
	img_hdr_t h;
	u8 row[40];

	fspi_init();
	if(!img_valid(xres, yres, &h) || img_stride(xres)>sizeof(row)){
		fspi_exit();
		return -1;
	}
	int stride = img_stride(xres);
	for(int y=0; y<yres; y++){
		sf_read(IMG_DATA+y*stride, stride, row);
		for(int x=0; x<xres; x++){
			draw_pixel(x, y, (row[x>>3] & (0x80>>(x&7))) ? WHITE : BLACK);
		}
	}
	fspi_exit();
	return 0;
}

// Persisted display mode: 0 = clock, 1 = image
int img_mode_get(void)
{
	u8 rec[2];

	fspi_init();
	sf_read(IMG_MODE, 2, rec);
	fspi_exit();
	return (rec[0]==0xa5 && rec[1]==1) ? 1 : 0;
}

void img_mode_set(int mode)
{
	if(img_mode_get()==mode) return;
	u8 rec[2] = {0xa5, (u8)mode};

	fspi_init();
	sf_sector_erase(ERASE_4K, IMG_MODE, 1);
	sf_page_write(IMG_MODE, rec, 2);
	sf_wait();
	fspi_exit();
}
