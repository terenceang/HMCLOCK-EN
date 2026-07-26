# HEMA HINK-E0213A41-FPC Pinout

2.13" monochrome e-paper panel (104 x 212, per `epd_hw_init(..., 104, 212, ... ROTATE_3)` in
`src/user_peripheral.c:201`, labeled `// 2.13 board BW` in `src/epd/epd.c:442`). 24-pin FPC
connector, referred to as **U5** on the HMCLOCK schematic.

## FPC connector (U5, 24 pins)

Pin order taken directly from the panel's connector legend (`Hardware/pinout_0.pdf` /
`pinout_1.pdf`, page 2), sequential pin 1 -> 24:

| U5 pin | Signal   | Description |
|-------:|----------|-------------|
| 1      | HLT_CTL  | Heater control |
| 2      | GDR      | Gate driver |
| 3      | RESE     | Reserved |
| 4      | VGL      | Gate low voltage |
| 5      | VGH      | Gate high voltage |
| 6      | TSCL     | On-panel thermal sensor I2C clock |
| 7      | TSDA     | On-panel thermal sensor I2C data |
| 8      | BS       | SPI mode select: 1 = 3-wire, 0 = 4-wire |
| 9      | nBUSY    | Busy (active low) |
| 10     | nRST     | Reset (active low) |
| 11     | D/C      | Data / command select |
| 12     | nCS      | Chip select (active low) |
| 13     | SCLK     | SPI clock |
| 14     | SDI      | SPI data in |
| 15     | VDDIO    | I/O supply |
| 16     | VCI      | Analog supply |
| 17     | VSS      | Ground |
| 18     | VDDIO    | I/O supply |
| 19     | VPP      | Programming/OTP supply |
| 20     | VSH      | Source high voltage |
| 21     | PREVGH   | Pre-charge gate high |
| 22     | VSL      | Source low voltage |
| 23     | PREVGL   | Pre-charge gate low |
| 24     | VCOM     | Common voltage |

`TSCL`/`TSDA` (temperature-compensation I2C) run to the panel's own thermal sensor and are not
driven by the DA14585 in this firmware.

## MCU (DA14585) <-> U5 mapping

The active configuration is set by the first `epd_hw_init()` call in
`src/user_peripheral.c:201` — `epd_hw_init(0x23200700, 0x05210006, ...)` — confirmed by decoding
against `RESERVE_GPIO` in `src/platform/user_periph_setup.c:78-84`. This is the pinout in
`Hardware/pinout_1.pdf`.

| U5 pin | Signal | MCU pin | Header pin (40-pin) |
|-------:|--------|---------|----------------------|
| 9      | nBUSY  | P2.0    | 40 |
| 10     | nRST   | P0.7    | 10 |
| 11     | D/C    | P0.5    | 7  |
| 12     | nCS    | P2.1    | 8  |
| 13     | SCLK   | P0.0    | 1  |
| 14     | SDI    | P0.6    | 9  |
| —      | PWR_EN | P2.3    | 18 (panel power switch, not a U5 pin) |

SCLK and SDI are shared with the board's onboard SPI flash (`SPI.CLK`/`SPI.SI` on the same
P0.0/P0.6 pins); the flash and the panel are distinguished by separate chip-select lines
(flash CS = P0.3, panel nCS = P2.1).

### Alternate wiring (fallback / 5-test-point variant)

`src/user_peripheral.c:203` falls back to `epd_hw_init(0x23111000, 0x07210120, ...)` if the
panel isn't detected on the primary pinout. This matches `Hardware/pinout_0.pdf` instead:

| Signal | MCU pin |
|--------|---------|
| nBUSY  | P1.1 |
| nRST   | P1.0 |
| D/C    | P0.7 |
| nCS    | P2.1 |
| SCLK   | P0.1 |
| SDI    | P2.0 |
| PWR_EN | P2.3 |

## Sources
- `Hardware/pinout_1.pdf` — primary board pinout (matches this document's main table)
- `Hardware/pinout_0.pdf` — alternate/fallback board pinout
- `src/epd/epd_hw.c` — `epd_hw_init()` config-word decoding (`group = pin >> 4`, `index = pin & 0xf`)
- `src/user_peripheral.c:196-205` — active `epd_hw_init()` calls
- `src/platform/user_periph_setup.c:78-84` — `GPIO_reservations()`
