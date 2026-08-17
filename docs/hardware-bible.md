# FreeWili 1 OG — Hardware Reference

*Consolidated from vendor docs, reverse engineering, Python API source, and datasheets.*

---

## Device Identity

The FreeWili 1 is a dual-RP2040 embedded security/development tool made by **Intrepid Control Systems, Inc.** (USB Vendor ID `093c`).

### USB Enumeration

| Component | VID:PID | Description |
|-----------|---------|-------------|
| Intrepid Hub | `093c:*` | Internal USB hub |
| FT232H (FPGA) | `0403:6014` | FTDI high-speed USB for FPGA path |
| RP2040 CDC | `2e8a:000a` | Serial CDC (Main + Display) |
| RP2040 UF2 | `2e8a:0003` | BOOTSEL mode for flashing |

### COM Port Mapping (Windows)

| Port | Interface | Notes |
|------|-----------|-------|
| COM9 | Display UART0 (FTDI) | IPP link, stdio disabled |
| COM12 | Main USB CDC | Requires DTR+RTS enabled |
| COM13 | ESP32-C6 USB CDC | GhostESP direct CLI, 115200 baud |

---

## Architecture

### Three Processors

| Processor | Chip | Role |
|-----------|------|------|
| **Main** | RP2040 | Tool engine, radios, FPGA, GPIO, serial console |
| **Display** | RP2040 | TFT, buttons, LEDs, sensors, audio, IR |
| **Orca** | ESP32-C6 | WiFi + BLE (GhostESP firmware, plugs into header) |

```
USB-C -> Internal Hub -> RP2040 Main -> FPGA -> GPIO Header
                      -> RP2040 Display -> LCD + Buttons + LEDs
                      -> FT232H -> FPGA data path
                      -> ESP32-C6 Orca (via UART header)
```

### ICs on Board

| IC | I2C Addr | Bus | Function |
|----|----------|-----|----------|
| LIS3DH | 0x19 | Display I2C1 | 3-axis accelerometer |
| PCA9555 | 0x21 | Display I2C1 | 16-bit I/O expander (level shifter DIR, header power) |
| BQ25892 | 0x6B | Display I2C1 | Battery charger / power path management |
| MCP7940 | 0x6F | Display I2C1 | Real-time clock |
| CC1101 x2 | SPI0 | Main | Sub-GHz ISM transceivers (315/433/868/915 MHz) |
| iCE40UP5K | SPI1 | Main | FPGA — gates all breakout signals through io_buffer |

---

## Main RP2040 GPIO Map

| GPIO | Function | Notes |
|------|----------|-------|
| GP0 | UART0 TX | IPP to Display |
| GP1 | UART0 RX | IPP from Display |
| GP2 | UART0 CTS | IPP flow control |
| GP3 | UART0 RTS | IPP flow control |
| GP4 | Display RUN | Reset Display (pulse low 10ms) |
| GP5 | Radio 2 CS | CC1101 #2 chip select |
| GP6 | SPI0 SCK | Shared radio SPI clock |
| GP7 | SPI0 MOSI | Shared radio SPI data out |
| GP8 | UART1 TX | To Orca / external header |
| GP9 | UART1 RX | From Orca / external header |
| GP10 | UART1 CTS | Flow control |
| GP11 | UART1 RTS | Flow control |
| GP12 | SPI1 MISO / FPGA | FPGA SPI or bit-bang fallback |
| GP13 | SPI1 CS | External header pin 1 |
| GP14 | SPI1 SCK | External header pin 15 |
| GP15 | SPI1 MOSI | External header pin 13 |
| GP16 | I2C0 SDA | External header pin 10 |
| GP17 | I2C0 SCL | External header pin 8 |
| GP18 | Radio 1 CS | CC1101 #1 chip select |
| GP19 | SPI0 MISO | Shared radio SPI data in |
| GP20 | FPGA CDONE | Bitstream load complete |
| GP21 | FPGA CRESET | Reset FPGA (active low) |
| GP22 | FPGA SS | SPI slave select for bitstream |
| GP23 | Radio GDO0 | CC1101 interrupt |
| GP24 | External GPIO | Header pin 14 (input) |
| GP25 | External GPIO | Header pin 17 (output) |
| GP26 | External GPIO | Header pin 14 (alt) |
| GP27 | External GPIO | Header pin 3 (output) |

---

## Display RP2040 GPIO Map

| GPIO | Function | Notes |
|------|----------|-------|
| GP0 | UART0 TX | IPP to Main |
| GP1 | UART0 RX | IPP from Main |
| GP2 | UART0 CTS | |
| GP3 | UART0 RTS | |
| GP4 | I2C1 SDA | Sensors + charger |
| GP5 | I2C1 SCL | |
| GP6 | WS2812 data | 7 RGB LEDs (PIO, inverted output) |
| GP7 | Audio PWM | Speaker |
| GP8 | TFT DC | Data/command select |
| GP9 | IR TX | PIO-driven NEC protocol |
| GP10 | TFT CS | Chip select |
| GP11 | TFT SCK | SPI1 clock (62.5 MHz) |
| GP12 | TFT MOSI | SPI1 data |
| GP13 | TFT RST | Display reset |
| GP14 | BTN_GRAY | Active low, pull-up |
| GP15 | BTN_YELLOW | Active low, pull-up |
| GP16 | IR RX | GPIO interrupt |
| GP22 | BTN_GREEN | Active low, pull-up |
| GP23 | BTN_BLUE | Active low, pull-up |
| GP24 | BTN_RED | Active low, pull-up |
| GP25 | TFT backlight | PWM brightness |

---

## 20-Pin External Header

| Pin | Function | Main GPIO |
|-----|----------|-----------|
| 1 | SPI1 CS | GP13 |
| 2 | 5V out | — |
| 3 | GPIO out | GP27 |
| 4 | V_REF IN | — (required for GPIO) |
| 5 | UART1 RX | GP9 |
| 6 | 3.3V out | — |
| 7 | UART1 CTS | GP10 |
| 8 | I2C0 SCL | GP17 |
| 9 | UART1 TX | GP8 |
| 10 | I2C0 SDA | GP16 |
| 11 | UART1 RTS | GP11 |
| 12 | SPI1 MISO | GP12 |
| 13 | SPI1 MOSI | GP15 |
| 14 | GPIO in | GP26 |
| 15 | SPI1 SCK | GP14 |
| 16 | SWCLK | Direct (no level shifter) |
| 17 | GPIO out | GP25 |
| 18 | SWDIO | Direct (no level shifter) |
| 19-20 | GND | — |

---

## Display

- 320x240 TFT, RGB565, SPI1 at 62.5 MHz
- ILI9341/ST7789 compatible init sequence
- Direct-to-TFT rendering (no framebuffer)
- Two fonts: 5x8 (status bar) and 6x10 (main content, 53 chars/line)
- Landscape orientation (MADCTL 0x68)

### Color Palette

| Name | Hex | Usage |
|------|-----|-------|
| COL_BG | `#000000` | Background |
| COL_TEXT | `#DEE2E6` | Primary text |
| COL_DIM | `#8C9196` | Secondary text |
| COL_HIGHLIGHT | `#2CB5FF` | Selected items |
| COL_HL_BG | `#092830` | Selection background |
| COL_ACTIVE | `#00FF00` | Active/running |
| COL_WARN | `#FFA500` | Warnings |
| COL_ERROR | `#FF0000` | Errors |
| COL_DISABLED | `#4A4A4A` | Unavailable |
| COL_ACCENT | `#B4DFFF` | Icons, labels |

---

## Buttons

5 physical buttons, active low with pull-ups, debounced at 20ms:

| Button | GPIO | Function |
|--------|------|----------|
| Gray | GP14 | Context action / Power off (3s hold) |
| Yellow | GP15 | Navigate up |
| Green | GP22 | Select / Confirm |
| Blue | GP23 | Navigate down |
| Red | GP24 | Back / Cancel |

---

## Power

- **Battery required** — USB alone doesn't power the device
- BQ25892 manages charging, power path, BATFET disconnect
- Gray held 3s triggers power off (BATFET disconnect via I2C)
- VBUS_GD (REG11 bit 7) detects USB presence — blocks power off when USB connected
- Battery percentage: linear interpolation 3300mV (0%) to 4150mV (100%)

---

## IPP (Inter-Processor Protocol)

Binary framing protocol between Main and Display processors.

### Frame Format

```
[SYNC:0xF1][SEQ:1][TYPE:1][LENGTH:2-LE][PAYLOAD:0-4096][CRC:2]
```

- CRC-16/CCITT (polynomial 0x1021, init 0xFFFF) over TYPE+LENGTH+PAYLOAD
- UART0 at 115200 baud with hardware flow control
- ISR-driven receive with state machine

### Message Types

| Range | Direction | Examples |
|-------|-----------|----------|
| 0x01-0x3F | Main -> Display | MENU_SHOW, STATUS_BAR, TEXT_SCREEN, TOAST, BACKLIGHT, LED_SET, IR_SEND, DRAW_TEXT, DRAW_RECT |
| 0x41-0x5F | Main -> Orca | PING, WIFI_SCAN_START |
| 0x81-0xBF | Display -> Main | BUTTON_EVENT, BATTERY_DATA, IR_RECEIVED, I2C_SCAN_RESP |
| 0xC1-0xDF | Orca -> Main | WIFI_AP_FOUND, PONG, BLE_ADV_FOUND |

---

## FPGA (iCE40UP5K)

No NVCM bitstream — Main loads a pass-through bitstream via SPI slave configuration at boot:

1. Assert CRESET low, SS low (slave mode)
2. Release CRESET, wait 3ms
3. Send bitstream via SPI1
4. Verify CDONE high
5. **Write io_dir register (0x6D, 0x01)** — enables bidirectional signal routing

Without the io_dir write, the FPGA blocks all breakout signals.

---

## Orca (ESP32-C6) Power Enable

The PCA9555 I/O expander controls level shifter direction and header power:

- Port 0 = 0xDA: UART level shifter DIR (TX/RTS output, RX/CTS input)
- Port 1 = 0xB8: I2C pullup, GPIO25, antenna paths

Both writes happen in `probe_orca()` before the IPP ping sequence.

---

## CC1101 Radio

Minimal SPI driver for the TI CC1101 sub-GHz transceiver:

- SPI0 shared between two radios (CS: GP18, GP5)
- Frequency word: `freq_word = freq_hz * 65536 / 26000000`
- RSSI conversion: `(raw / 2) - 74` dBm
- Scanner config: 325 kHz bandwidth, OOK, no sync, auto-cal on IDLE->RX
- Supported bands: 315, 433, 868, 915 MHz

---

## Serial Console

Main RP2040 USB CDC (COM12, requires DTR+RTS):

| Key | Function |
|-----|----------|
| `s` | Status dump |
| `o` | Re-probe Orca |
| `B` | Reboot Display to bootloader |
| `R` | Hardware reset Display |
| `F` | Re-write FPGA io_dir |
| `T` | UART1 passthrough (triple-ESC to exit) |
| `L` | List WASM apps |
| `U` | Upload file to filesystem |
| `X` | Delete file from filesystem |
| `d` | UART1 dump (3s listen) |
| `D` | Multi-baud dump |
| `i` | IPP ping |

---

## Flashing

### Main RP2040

1. 1200-baud DTR touch on COM12 -> RPI-RP2 drive appears
2. Copy `.uf2` to the drive
3. Device reboots

### Display RP2040

1. Send `B` on COM12 at 115200 baud -> Main commands Display into bootloader via IPP
2. RPI-RP2 drive appears
3. Copy `.uf2` to the drive

**Flash Display first, then Main.**

---

## Flash Filesystem

LittleFS on internal flash, starting at offset 0x100000.

- Apps stored at `/apps/`
- Settings in last flash sector (0xFFF000-0xFFFFFF) — separate from filesystem
- Max file size for uploads: 32 KB
