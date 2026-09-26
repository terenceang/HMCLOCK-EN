# Changelog

## 0xA50f002f

* **Advertising every 15 minutes** (:00, :15, :30, :45) instead of every 10, and the Bluetooth icon no longer triggers an extra panel refresh when the advertising burst ends (it clears at the next minute redraw). Saves roughly 5 uA of average current.
* **Bluetooth icon** now shows while advertising or connected, in the top-right of the left card, on both the clock and the pairing screen.
* **Advertising fixes:** the advertising state is also cleared by the SDK timeout hook and on connect, so a missed complete event can no longer block later bursts; advertising is not started while connected or during a firmware update, and the panel stays untouched during an update.
* **Time sync:** a rejected (out-of-range) time no longer redraws the clock, calibration is ignored before the first sync, and the web app reads back the state and reports a rejected sync and a dropped link.

## 0xA50f0027

* **Advertising cleanup:** the advertising state is always cleared when advertising completes, and the version tag in the advertising data uses company ID `0xFFFF`.
* **Docs:** the supported-screens table now merges the Hema ESL model list with tested status (A41, A53 and A07 tested working).

## 0xA50f0026

* **Black and white only.** Removed the tri-colour (red plane) code and the panel-diagnostic experiments (waveform dump, temperature and refresh-time read-outs), which frees about 5 KB of RAM. The panel-size override stays.
* **Web app:** QR deep link to one clock (`?n=DCLK-XXYYZZ`), optional auto time sync on connect, the panel size shown with a size override and a Test pattern button in the Display card, firmware version and update merged into the Connection card, the clock's firmware version and the web build in the page, and a firmware update that detects a rejected image, verifies the running version and reconnects by itself.
* **Firmware update hardening:** an update aborts cleanly on disconnect, ignores stray commands, validates the product header and slot size, and only programs the image header once the CRC has passed.
* The pairing (QR) screen scales to the panel (larger QR on 296 x 128).
* Build from the command line: see `Programming.md`.

## 0xA50f0015

* The clean-up frame that precedes an image draw now uses the fast waveform instead of a full refresh, shortening an image change to roughly one full refresh plus a fast one.

## 0xA50f0014

* **Fixed the image being garbled when drawn over other panel content.** A picture painted directly over the clock face (or a previous picture) left half-driven, noisy pixels, while a cold boot or a draw after the clock looked fine. Showing or uploading an image now first drives the panel solid black with a full refresh (the same reset used by the nightly scrub) and paints the picture when that finishes, so an image change takes two refreshes (about twice as long). The boot-time restore is unchanged.

## 0xA50f0013

* The advertising window before the first time sync is now 30 s (was 15 s), on cold boot and on each per-minute restart while unsynced, giving more time to connect. After the first sync the 15 s burst is unchanged.

## 0xA50f0012

* **New image display mode, mutually exclusive with the analog clock + calendar.** The web app can now convert any picture to the panel size (letterboxed, Floyd-Steinberg dithered, 1-bit black & white) and upload it; the device stores it in SPI flash (`0x3B000-0x3EFFF`, clear of the firmware slots) and shows it until you switch back. The chosen mode and image survive power loss. In image mode the minute timer keeps the time but no longer redraws the panel (no nightly scrub either); low-battery shutdown still applies.
* New BLE commands on `0xFF01`: `0x93` set mode (0 clock / 1 image), `0x94` begin upload (panel w, h), `0x95` chunk (seq, up to 128 bytes), `0x96` finish (CRC32 - the image is only stored and shown if length and CRC match, and the CRC is re-verified from flash). A dropped connection mid-upload cancels it.
* The clock-data notification/read (`0xFF01`) grew from 11 to 16 bytes: panel width/height (u16 each) and the current display mode. The web app uses these to size the picture and show the mode.

## 0xA50f0011

* **The pairing (QR) and low-battery screens now scale to the panel.** `QR_draw()` had every position hard-coded for the smallest 212×104 panel, so on the 250×122 and 296×128 variants the pairing screen left a dead band down the right side and across the bottom. Layout is now derived from the detected panel resolution, exactly like the clock screen. The 212×104 rendering is pixel-identical to before; larger panels fill the screen (the text column and divider stretch to the panel edges, the QR block is vertically centered).
* Fixed the low-battery screen overflowing short panels: the battery QR was drawn at scale 4 (124 px tall) regardless of panel height, clipping on the 212×104 panel; it now picks scale 4 only where it fits (296×128) and centers itself.

## 0xA50f0010

* **Fixed negative time-calibration offsets being encoded incorrectly by the web app.** The calibration writer computed the high byte of the signed 16-bit offset with a fractional division (`diff/256`), which truncates toward zero in JS — a device running *fast* by N seconds received `+256−N`-ish corrections in the wrong direction (e.g. −10 s became +246 s), making drift *worse*. Encoding now uses proper two's-complement bitwise ops.
* Removed the unused BLE `0x90` toggle-12/24-hour-format command and its "Toggle Format" web button: the firmware set a format flag that was never read anywhere (the clock is analog), so the command did nothing.
* Web app (both EN and CN): the Bluetooth availability check + error banner that only existed on the published GitHub Pages copy is now in the source versions — Connect starts disabled until a radio is confirmed, and connect failures show an actionable message (no adapter / unsupported browser / picker cancelled / insecure context / lost connection).
* Web app robustness: the calibration button no longer stays permanently disabled if the device disconnects mid-calibration (try/finally); the exact-second wait before time sync no longer busy-spins the UI thread; device time derivation unified into one helper (day rollover now displays correctly during long sessions); removed dead code (unused FF03 characteristic fetch, redundant system-time write on connect).
* Firmware cleanup: the e-paper update-commit sequence (4 copies) and framebuffer clear (3 copies) are deduplicated into `epd_commit()`/`fb_clear()` helpers; removed unused retained-memory globals left over from the SDK template.
* Repo hygiene: `docs/index.html` (the published page) is now generated from `WebApp/WebApp_EN/index.html` by a GitHub Actions workflow (`.github/workflows/sync-docs.yml`) — edit the WebApp_EN copy only; the published site syncs automatically.

## 0xA50f000f

* **Fixed firmware not persisting across resets on some units.** The stock product header on these units points the two firmware slots at overlapping addresses (`0x2000`/`0x4000`), and the boot chain boots slot 0 regardless of generation id — so self-flash and OTA updates written to the "inactive" slot were ignored, and the device reverted to the old firmware on every reset. Self-flash now installs the build into the primary slot (slot 0 from the product header) with a generation id one above any existing slot, so it wins under every booter selection policy. See `Programming.md` for the full analysis.
* Generation ids are now wrap-safe: the id was previously incremented without a check, so after enough updates it passed `0x7F` and became `0x80` (`-128` as a signed char), ranking the new image *below* every other image.
* Added a readable BLE diagnostics characteristic **FF04** (32 bytes) exposing the boot/OTP marker, boot header, running version, and what the last boot's flash update did (install vs up-to-date skip, and which field mismatched). Documented in `Programming.md`.
* Added `Programming.md`: how firmware persistence works, how to program the device permanently (debugger RAM load or OTA), flash layout, and the FF04 diagnostic reference.
* Pairing screen polish: rows re-spaced to be symmetric about the QR center using the real font metrics (the version row no longer touches the bottom edge), version string now uppercase (`vA50F000E`), the uneven text-dash divider replaced with a crisp drawn line on the optical center, and a battery-level glyph added next to the version.
* Clock face polish: the dial no longer touches the card frame (2 px breathing room), and the pivot hub shrank from a chunky 5×5 to a 3×3 square so it doesn't outweigh the 1 px hands.
* Nightly ghost scrub: at midnight the panel is driven solid black with a full refresh (then the face is restored with a forced full update on the next minute tick), which clears the e-paper ghosting — including stock-image residue like the red price-tag highlights — that fast/partial updates leave behind.
* Re-enabled UART console logging (`CFG_PRINTF`) in the DA14585 config for debugging; note that leaving it on blocks extended sleep whenever a log line prints, so turn it off for power-sensitive builds.

## 0xA50f000e

Fixes and reductions from a power-efficiency review (see the review's own findings for full detail; no changes were made to items requiring hardware validation this pass):

* Fixed the low-battery auto-shutdown check (`app_clock_timer_cb`): it compared the wrong variable (`flags`, still holding its pre-computed default) instead of `stat`, so the periodic battery check — and the safety mechanism that stops the device waking every minute once the battery is critical — never ran.
* Redesigned the pairing/QR screen: version string, "Scan to Pair" wording, and picks a fast or full e-paper update mode per redraw instead of always forcing a full (slower, higher-current) refresh.
* Increased BLE slave latency from 0 to 4 connection events, so the radio skips most empty connection events instead of waking every 10-20ms for the whole time a phone is connected (ample headroom versus the 2500ms supervision timeout).
* Shortened the pairing/re-advertise burst window from 30s to 15s (`advertise_period`) — still a generous connect window, at roughly half the previous advertising duty cycle.
* `epd_cmd1`-`epd_cmd4` now correctly deassert chip-select at the end of each transaction, matching `epd_cmd`/`epd_data`.
* The EPD `nBUSY` (active-low) input is now pulled up instead of left floating while the panel is powered off between updates.
* The boot-time panel-detect wait (`epd_wait()`) is now bounded (~1s) instead of an unbounded spin, so a missing/faulty panel can't hang boot forever.
* The DA14586 build no longer forces UART console logging on; its unconditional `printk()` calls (not gated behind debug-only branches) were blocking entry to extended sleep on nearly every tick. Matches the 531/535 configs, which already leave it off by default.

## 0xA50f000d

* Fixed a broken placeholder check (`year==2025 && month<=5`, always false) that let the clock face display before the time had ever been synced; the "not yet configured" state is now driven by whether a `clock_set` write has ever been received.
* The pairing/QR screen now shows the Bluetooth icon while advertising, matching the clock face's existing behavior.
* Pairing-mode advertising now follows the same 30-second-burst-every-10-minutes cadence as the synced clock, instead of restarting on every 1-minute tick.

## 0xA50f000c

* Redesigned the clock screen: an analog clock face card on the left, an iOS-style calendar icon card on the right (weekday header, big day-of-month, month + year), both sized to fill nearly the whole screen.
* Moved the battery and Bluetooth status icons into the clock card's top-left corner.
* Fixed a `draw_pixel()` bug where `WHITE` was a no-op (it only ever cleared pixels, never set them), which silently broke drawing white text over a black-filled background — needed for the calendar card's inverted weekday header.
* Added bold and letter-spaced ("kerned") text-drawing primitives to `epd_gui.c`, used to keep the calendar card's small text legible at this size.
* Added an English translation of the Web Bluetooth control page (`WebApp/WebApp_EN/webApp.html`), alongside the existing Chinese one.
* Added a Light/Dark/System theme toggle to both web app versions.
* Added a matching analog-clock + calendar-card preview to both web app versions, shown by default — driven by the connected device's time, or system time when not connected.

## 0xA50f000b

* Translated the remaining Chinese code comments to English across `epd.c`, `epd_gui.c`, `spi_flash.c`, `user_custs1_impl.c`, `user_peripheral.c`.
* Added hardware pinout documentation (`Hardware/HINK-E0213A41-FPC.md`) and pinout reference PDFs.
* Added the initial Web Bluetooth control-center web app (`WebApp/WebApp_CN/webApp.html`) for connecting, time sync, firmware update, and calibration.

## 0xA50f000a

* Bolded the 7-segment clock digits (`font50.h`/`font66.h`) by dilating each glyph 1px inward on every stroke; glyph dimensions/advance widths unchanged.

## 0xA50f0009

* Removed the lunar calendar / solar-term / traditional-holiday feature entirely (lunar date tables, solar-term calculation, holiday table, and their on-screen display).
* Converted the on-screen date to English (ISO `YYYY-MM-DD` plus a 3-letter weekday, e.g. `2026-07-18  Sat`) and AM/PM to `AM`/`PM`.
* Renamed the BLE device from `DLG-CLOCK-XXYYZZ` to `DCLK-XXYYZZ`.
* Regenerated the pairing-screen QR code to point at the permanent hosted web app: `https://terenceang.github.io/HMCLOCK-EN/`.
* `clock_set` (BLE `0x91`) no longer reads/validates the lunar-date bytes; the minimum accepted write length dropped from 12 to 9 bytes. The web app no longer computes or sends them.

## 0xA50f0008

* Removed a dead/unreachable duplicate branch in the BLE long-value write dispatcher.
* Added range validation on BLE-set clock/lunar-date fields (`0x91`), preventing out-of-bounds array reads from a malformed write before they could reach the lunar calendar lookup tables.
* Added minimum-length checks on incoming BLE writes before trusting fixed-offset fields, for the clock-set, calibration, and OTA commands.
* Removed the unused, already-diverged `spi_flash_hwctl.c` duplicate flash/OTA driver (it wasn't part of any build target and was missing a reset call its live counterpart had).
* OTA updates now verify a CRC32 of the received firmware image before resetting into it. A failed check aborts the update and invalidates the new image's header so a stray reset (watchdog, power blip) can't boot into a partially-written image.
