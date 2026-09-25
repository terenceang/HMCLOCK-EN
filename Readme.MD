# Hema ESL Clock

This is a fork of the original [HMCLOCK](https://github.com/tpunix/HMCLOCK) project by [tpunix](https://github.com/tpunix).

Fork maintained by Terence Ang - motionfxdesign.

---

![Device Showcase](./images/device.jpg)

## Project Introduction

This project utilizes discarded **Hema Electronic Shelf Label (ESL)** hardware from supermarkets to implement a simple yet practical electronic clock with the following features:

* ✅ Analog clock + calendar card display (weekday, day, month, year)
* ✅ Display battery level
* ✅ Bluetooth time synchronization
* ✅ Time calibration
* ✅ Bluetooth OTA firmware update

---

## Supported screens

The clock drives every panel in **black and white** only (the red plane of tri-colour panels is not driven); the table lists what has been tested. The panel size normally comes from the configuration record in flash (`0x3A000`). If the record is missing or wrong (for example after swapping the panel), pick the size in the web app's Display card (**Panel** dropdown, firmware 0xA50f0026 or later); the clock restarts to apply it. The **Test pattern** button next to it draws a border, corner blocks, size, grey/black swatches and line patterns to check the result.

| Size | Colors | Interface | Model | Main MCU | Resolution (landscape) | Disassembly | LED | Pinout | Status |
|------|--------|-----------|-------|----------|------------------------|-------------|-----|--------|--------|
| 2.13" | Black/White | Soldered | HINK-E0213A41/A55 | IL3897 / SSD1675B | 212×104 | Hard | Yes | `pinout_1/0.xlsx`, `Hardware/HINK-E0213A41-FPC.md` | Tested working (HINK-E0213A41-FPC). Reference panel, populated on the HMCLOCK board |
| 2.13" | Black/White | Soldered | HINK-E0213A53-FPC-A0 | ? | ? | ? | ? | ? | Tested working |
| 2.13" | Black/White | Socket | HINK-E0213A07-A1 | ? | ? | ? | ? | ? | Tested working |
| 2.13" | Black/White | Socket | OPM021B1 | IL3895 / SSD1673A | 250×122 | Hard | Yes | `pinout_0.xlsx` | Not tested |
| 2.13" | Black/White/Red | ? | HINK-E0213A22-A0 | ? | 250×122 | ? | ? | ? | Select `250 x 122`; black/white only. On the HMCLOCK board its contrast was weak, most likely because the board's boost circuit was sized for the A41 |
| 2.13" | Black/White/Red | Soldered | HINK-E0213A67 | IL3897 | 250×122 | Hard | 3-Color | `pinout_0.xlsx` | Not tested |
| 2.9" | Black/White/Red | Socket | HINK-E029A10 | IL3897 | 296×128 | Easy | Yes | `pinout_0.xlsx` | Not tested (layout only, not verified on hardware) |

Notes:

* Tri-colour (black/white/red) panels show black and white only; the red plane is not driven.
* The size cannot be read from the panel itself, so it must match the attached panel.
* Only these three sizes have a screen layout. Any other size falls back to the 212 x 104 layout.

---

## Changelog

See [CHANGELOG.md](CHANGELOG.md).

---

## Compilation and Flashing

Please download the DA14585 [SDK](https://www.renesas.com/en/document/swo/sdk60221401-da1453x-da145856 "SDK"), currently using version **6.0.22.1401**.

1. Place this project inside:

   ```
   SDK_PATH/projects/target_apps/ble_examples
   ```

2. Open the project with Keil MDK to compile.

3. Once compiled:

   * **Run in debug mode once**, and the firmware will be automatically written to Flash (see [`Programming.md`](Programming.md) for how persistence works and what to expect on the first boot);
   * Or use the **SmartSnippets Toolbox** to download the firmware to RAM and run it once;
   * Or flash it with **OpenOCD** (see below) if you're using a CMSIS-DAP/ST-Link probe instead of a J-Link.

### Flashing with OpenOCD

The DA14585/586 has no internal flash — firmware normally runs from RAM, either loaded by a debugger or copied there by the ROM bootloader from external SPI flash/OTP. `Keil_5\openocd_da14585.cfg` drives this RAM-load-and-run path over SWD using a CMSIS-DAP-compatible probe (tested with the same probe configured under Keil's `CMSIS_AGDI` debug driver in this project).

Install OpenOCD (e.g. `winget install xpack-dev-tools.openocd-xpack`), then from the `Keil_5` directory:

```
openocd -f openocd_da14585.cfg -c "ram_load_run out_DA14585/Objects/ble_app_peripheral_585.axf" -c shutdown
```

This halts the core, writes the `.axf` into RAM at `0x07fc0000`, sets `SP`/`PC` from the vector table, and resumes — equivalent to Keil's RAM-debug flow. As with that flow, the app's own first-boot logic writes itself to external SPI flash, so it persists across power cycles without any explicit flash step.

Notes:
* OpenOCD has no flash driver for this chip's external SPI flash/OTP, so `program`/`flash write_image` won't work here — only the RAM-load path does.
* Use a plain `reset halt`, not a hardware reset + delay: a core-only reset (`SYSRESETREQ`) does **not** re-trigger the ROM's flash-to-RAM boot copy on this chip (that only happens on power-on-reset), so there's no race to wait out. Adding a delay before halting risks catching the core mid-execution and triggering a HardFault.

---

## Bluetooth Time Synchronization

The project's built-in web page provides **Web Bluetooth** functionality for manual time synchronization:

* The firmware broadcasts every **10 minutes on the dot**, lasting for **15 seconds**;
* During broadcasting, the screen displays a Bluetooth icon and the device name suffix;
* Open the web page, click "Connect", select your device, and click "Sync Time" to complete the synchronization.

## Time Calibration

According to observations, by default, the clock runs about two seconds fast per day. The accuracy can be improved by fine-tuning the timer interval.
After syncing the time, performing two calibrations 2 to 3 weeks apart will keep the clock running very accurately.

* Once the device is connected, click the "Time Calibration" button and wait about one minute.
* Perform a time synchronization immediately after calibration.

[![Control Panel](./images/Control%20Panel.png)](https://terenceang.github.io/HMCLOCK-EN/)

Open the [web control panel](https://terenceang.github.io/HMCLOCK-EN/) in Chrome or Edge (Web Bluetooth) to connect to the clock.

When powered on for the first time, it will show a pairing page. Scan the QR code to open the web page and pair.


---

## Case Assembly

Case design files are stored in the `3D/` directory. It features a sliding lid design, and 3D printing with resin is recommended.

---

## Flash Storage Layout

### Firmware slots and boot headers

The boot chain (ROM or the ESL's OTP boot loader) reads the **product header at
`0x38000`** (`70 52 ...`), which contains the start addresses of two firmware
image slots. Each slot begins with a 64-byte Dialog SUOTA image header
(`70 51 AA` + generation id, code size, CRC32, version at offset 28).

* The slot addresses are **not fixed** — read them from the product header.
  Most units use `0x4000`/`0x1F000`; some units use `0x2000`/`0x4000` with
  overlapping slots, and their boot chain boots slot 0 unconditionally.
* Self-flash (`selflash()` in `src/epd/spi_flash.c`) installs new builds into
  the primary slot with a wrap-safe generation id; see [`Programming.md`](Programming.md).
* A legacy single-image boot header (`70 50 ...`) may also exist at `0x00000`;
  on the units tested it is not used by the boot chain.

### Screen Pin Configuration (Address `0x39000`)

Example:

```
09 01 FF FF FF FF FF FF 21 22 10  01  20  07  11  23
                        CS ?? RST CLK SDI DC BUSY PWR
```

* First byte `09`: Screen Type
* Second byte `01`: Pin configuration enable flag (any value other than `01` is invalid)

### Screen Resolution and Info (Address `0x3a000`)

Example:

```
00 25 00 00 92 fa a8 fe 00 01 80 00 28 01 04 00
                          0080  0128          128x296  BWR
```

---

## Additional Notes

* The original ESL firmware is stored in the SUOTA section of Flash;
* Most ESLs use an OTP bootloader but will continue to load firmware from Flash;
* Flash contains the screen and pin configuration, so the new firmware does not need to hardcode this information;
* The stock firmware cannot be scanned via Bluetooth, making it difficult to update via OTA directly without initially flashing;
* Most ESL batteries are nearly depleted, so it is recommended to disassemble and replace the power supply.
