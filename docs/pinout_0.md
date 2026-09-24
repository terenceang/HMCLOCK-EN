# DA14585 module pinout (QFN40)

## Module pins

| Pin | Name | Board net | E-paper |
|---|---|---|---|
| 1 | P00 | SPI.CLK |  |
| 2 | P01 | U5.13 | SCLK |
| 3 | P02 | LED_Red |  |
| 4 | P03 | SPI.CS |  |
| 5 | P30 | - |  |
| 6 | P04 | UART_TX |  |
| 7 | P05 | SPI.SO | UART_RX |
| 8 | P21 | U5.12 | CS |
| 9 | P06 | SPI.SI |  |
| 10 | P07 | U5.11 | DC |
| 11 | 32Kp |  |  |
| 12 | 32Km |  |  |
| 13 | P22 | U5.1 | HLT_CTL |
| 14 | VBAT_RF |  |  |
| 15 | VBAT3V |  |  |
| 16 | GND |  |  |
| 17 | RST |  |  |
| 18 | P23 | PWR_EN |  |
| 19 | VDCDC |  |  |
| 20 | P24 | 接U4 (to U4) |  |
| 21 | SWITCH |  |  |
| 22 | P10 | U5.10 | RST |
| 23 | VBAT1V |  |  |
| 24 | P11 | U5.9 | BUSY |
| 25 | SWDIO |  |  |
| 26 | SWCLK |  |  |
| 27 | P12 | - |  |
| 28 | P13 | - |  |
| 29 | 16Mp |  |  |
| 30 | 16Mm |  |  |
| 31 | VDCDC_RF |  |  |
| 32 | P25 | LED_Green |  |
| 33 | P26 | - |  |
| 34 | RFIOm |  |  |
| 35 | RFIOp |  |  |
| 36 | P27 | - |  |
| 37 | P28 | - |  |
| 38 | VDD |  |  |
| 39 | P29 | LED_Blue |  |
| 40 | P20 | U5.14 | SDI |

## E-paper connector

FPC pin numbers as on the HMCLOCK sheet.

| GPIO | FPC pin | Signal | Note |
|---|---|---|---|
| P22 | 1 | HLT_CTL |  |
|  | 2 | GDR |  |
|  | 3 | RESE |  |
|  | 4 | VGL |  |
|  | 5 | VGH |  |
|  | 6 | TSCL | 温控IIC接口 (temperature sensor I2C, pins 6-7) |
|  | 7 | TSDA |  |
|  | 8 | BS | 1:3线  0:4线 (1 = 3-wire SPI, 0 = 4-wire) |
| P11 | 9 | nBUSY |  |
| P10 | 10 | nRST |  |
| P07 | 11 | D/C |  |
| P21 | 12 | nCS |  |
| P01 | 13 | SCLK |  |
| P20 | 14 | SDI |  |
|  | 15 | VDDIO |  |
|  | 16 | VCI |  |
|  | 17 | VSS |  |
|  | 18 | VDDIO |  |
|  | 19 | VPP |  |
|  | 20 | VSH |  |
|  | 21 | PREVGH |  |
|  | 22 | VSL |  |
|  | 23 | PREVGL |  |
|  | 24 | VCOM |  |

在Flash的0x39000处，保存有GPIO的配置信息 (GPIO configuration is stored in flash at 0x39000.)
