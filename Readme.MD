# Hema ESL Clock

This is a fork of the original [HMCLOCK](https://github.com/tpunix/HMCLOCK) project by [tpunix](https://github.com/tpunix).

---

![Device Showcase](./images/device.png)

## Project Introduction

This project utilizes discarded **Hema Electronic Shelf Label (ESL)** hardware from supermarkets to implement a simple yet practical electronic clock with the following features:

* ✅ Display date and time
* ✅ Display Lunar calendar, Solar terms, and traditional holidays
* ✅ Display battery level
* ✅ Bluetooth time synchronization
* ✅ Time calibration
* ✅ Bluetooth OTA firmware update

---

## Changelog

### 0xA50f0008

* Removed a dead/unreachable duplicate branch in the BLE long-value write dispatcher.
* Added range validation on BLE-set clock/lunar-date fields (`0x91`), preventing out-of-bounds array reads from a malformed write before they could reach the lunar calendar lookup tables.
* Added minimum-length checks on incoming BLE writes before trusting fixed-offset fields, for the clock-set, calibration, and OTA commands.
* Removed the unused, already-diverged `spi_flash_hwctl.c` duplicate flash/OTA driver (it wasn't part of any build target and was missing a reset call its live counterpart had).
* OTA updates now verify a CRC32 of the received firmware image before resetting into it. A failed check aborts the update and invalidates the new image's header so a stray reset (watchdog, power blip) can't boot into a partially-written image.

---

## Compilation and Flashing

Please download the DA14585 [SDK](https://www.renesas.com/en/document/swo/sdk60221401-da1453x-da145856 "SDK"), currently using version **6.0.22.1401**.

1. Place this project inside:

   ```
   SDK_PATH/projects/target_apps/ble_examples
   ```

2. Open the project with Keil MDK to compile.

3. Once compiled:

   * **Run in debug mode once**, and the firmware will be automatically written to Flash;
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

* The firmware broadcasts every **10 minutes on the dot**, lasting for **30 seconds**;
* During broadcasting, the screen displays a Bluetooth icon and the device name suffix;
* Open the web page, click "Connect", select your device, and click "Sync Time" to complete the synchronization.

## Time Calibration

According to observations, by default, the clock runs about two seconds fast per day. The accuracy can be improved by fine-tuning the timer interval.
After syncing the time, performing two calibrations 2 to 3 weeks apart will keep the clock running very accurately.

* Once the device is connected, click the "Time Calibration" button and wait about one minute.
* Perform a time synchronization immediately after calibration.

![Control Panel](./images/ctrl.png)

When powered on for the first time, it will show a pairing page. Scan the QR code to open the web page and pair.

![Pairing Prompt](./images/peiwang.png)

---

## Case Assembly

Case design files are stored in the `3D/` directory. It features a sliding lid design, and 3D printing with resin is recommended.

---

## About Hema ESL (Electronic Shelf Label)

The types of Hema ESL screens used in this project are listed below:

| Size | Colors | Interface Type | Model | Main MCU | Resolution | Disassembly Difficulty | LED | Pinout File |
| ------ | --- | ------ | ----------------- | ----------------- | ------- | ---- | --- | --------------- |
| 2.13" | Black/White | Soldered | HINK-E0213A41/A55 | IL3897 / SSD1675B | 212×104 | Hard | Yes | pinout_1/0.xlsx |
| 2.13" | Black/White | Socket | OPM021B1 | IL3895 / SSD1673A | 250×122 | Hard | Yes | pinout_0.xlsx |
| 2.13" | Black/White/Red | Soldered | HINK-E0213A67 | IL3897 | 250×122 | Hard | 3-Color | pinout_0.xlsx |
| 2.9" | Black/White/Red | Socket | HINK-E029A10 | IL3897 | 296×128 | Easy | Yes | pinout_0.xlsx |


---

### Flash Storage Layout

#### Screen Pin Configuration (Address `0x39000`)

Example:

```
09 01 FF FF FF FF FF FF 21 22 10  01  20  07  11  23
                        CS ?? RST CLK SDI DC BUSY PWR
```

* First byte `09`: Screen Type
* Second byte `01`: Pin configuration enable flag (any value other than `01` is invalid)

#### Screen Resolution and Info (Address `0x3a000`)

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
