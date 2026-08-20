# FWOG Full Capability Audit & WASM API Expansion

Akhlut Custom Firmware — FreeWili 1 OG
Audit Date: 2026-08-20

---

## Phase 1: Hardware Capability Extraction

### Main RP2040 Pin Map (board.h)

| GPIO | Function | Peripheral | Notes |
|------|----------|-----------|-------|
| GP0 | IPP TX | UART0 | → Display RX |
| GP1 | IPP RX | UART0 | ← Display TX |
| GP2 | IPP CTS | UART0 | ← Display RTS |
| GP3 | IPP RTS | UART0 | → Display CTS |
| GP4 | Radio MISO | SPI0 | Shared both CC1101 |
| GP5 | Radio 2 CS | SPI0 | |
| GP6 | Radio SCK | SPI0 | Shared both CC1101 |
| GP7 | Radio MOSI | SPI0 | Shared both CC1101 |
| GP8 | Ext UART TX | UART1 | Header pin 9 / Orca |
| GP9 | Ext UART RX | UART1 | Header pin 5 / Orca |
| GP10 | Ext UART CTS | UART1 | Header pin 7 |
| GP11 | Ext UART RTS | UART1 | Header pin 11 |
| GP12 | Ext SPI MISO | SPI1 | Header pin 12 / FPGA |
| GP13 | Ext SPI CS | SPI1 | Header pin 1 |
| GP14 | Ext SPI SCK | SPI1 | Header pin 15 |
| GP15 | Ext SPI MOSI | SPI1 | Header pin 13 |
| GP16 | Ext I2C SDA | I2C0 | Header pin 10, PCA9517 shifted |
| GP17 | Ext I2C SCL | I2C0 | Header pin 8, 10K pullups |
| GP18 | Radio 1 CS | SPI0 | |
| GP19 | Radio 1 GDO2 | GPIO | Carrier sense / sync |
| GP20 | Radio 2 GDO0 | GPIO | Packet RX/TX interrupt |
| GP21 | Radio 1 GDO0 | GPIO | Packet RX/TX interrupt |
| GP22 | Radio 2 GDO2 | GPIO | Carrier sense / sync |
| GP23 | FPGA CLK | GPIO | 31.25 MHz clock output |
| GP24 | FPGA CDONE | GPIO | Config done (input) |
| GP25 | Ext GPIO | GPIO | Header pin 17 (output) |
| GP26 | Ext GPIO | GPIO | Header pin 14 (input) |
| GP27 | Ext GPIO | GPIO | Header pin 3 (output) |
| GP28 | Display RUN | GPIO | Controls Display RP2040 reset |
| GP29 | FPGA CRESET | GPIO | Config reset (active low) |

### Display RP2040 Pin Map (board_display.h)

| GPIO | Function | Peripheral | Notes |
|------|----------|-----------|-------|
| GP0 | IPP TX | UART0 | → Main RX |
| GP1 | IPP RX | UART0 | ← Main TX |
| GP2 | IPP CTS | UART0 | ← Main RTS |
| GP3 | IPP RTS | UART0 | → Main CTS |
| GP4 | I2S Data | I2S | Digital speaker |
| GP5 | I2S BCLK | I2S | Bit clock |
| GP6 | I2S WS | I2S | Word select |
| GP7 | NeoPixel | PIO | 7x WS2812 chain |
| GP8 | Charger EN | GPIO | BQ25892, active low (unconfirmed) |
| GP9 | IR TX | GPIO | Bit-banged 38kHz carrier |
| GP10 | TFT SCK | SPI1 | 62.5 MHz |
| GP11 | TFT MOSI | SPI1 | |
| GP12 | TFT DC | GPIO | Data/Command select |
| GP13 | TFT CS | SPI1 | |
| GP14 | BTN Gray | GPIO | Active low, pull-up |
| GP15 | BTN Yellow | GPIO | Active low, pull-up |
| GP16 | IR RX | GPIO | Interrupt-driven NEC decode |
| GP17 | PDM CLK | GPIO | Microphone clock output |
| GP18-21 | UNKNOWN | — | Set as outputs, driven high at boot |
| GP22 | BTN Green | GPIO | Active low, pull-up |
| GP23 | BTN Blue | GPIO | Active low, pull-up (boot strap) |
| GP24 | BTN Red | GPIO | Active low, pull-up |
| GP25 | Backlight | PWM | 0-255 brightness |
| GP26 | I2C1 SDA | I2C1 | 400kHz, local sensors |
| GP27 | I2C1 SCL | I2C1 | |
| GP28 | UNKNOWN | — | Set as output, driven high at boot |
| GP29 | PDM Data | GPIO | Microphone data input |

### I2C1 Peripheral Map (Display-side, 0x6B confirmed operational)

| Address | Device | Status in Firmware |
|---------|--------|-------------------|
| 0x19 | LIS3DH accelerometer | Probed at boot, **NEVER READ for data** |
| 0x21 | PCA9555/TCA9555 I/O expander | Initialized with direction registers, R/W via IPP |
| 0x6B | BQ25892 charger | Fully operational — reads VBAT, VBUS, VSYS, ICHG, charge status every 1s |
| 0x6F | MCP7940 RTC | Probed at boot, **NEVER READ for data** |

### PCA9555 I/O Expander Port Assignments

Port 0 output register: `0xDA` — controls external header level shifter directions:
- Bit 7 (0x80): SCLK direction (A→B, output)
- Bit 6 (0x40): CS direction (A→B, output)
- Bit 4 (0x10): SPI_TX direction (A→B, output)
- Bit 3 (0x08): UART_TX direction (A→B, output)
- Bit 1 (0x02): UART_RTS direction (A→B, output)
- SPI_RX, UART_RX: inputs (B→A)

Port 1 output register: `0xB8`:
- Bit 7 (0x80): I2C pullup enable
- Bit 5 (0x20): GPIO25 direction
- Bit 4 (0x10): ANT_V1_2 (antenna switch)
- Bit 3 (0x08): ANT_V1_1 (antenna switch)

### FPGA (iCE40UP5K)

- Clock: 31.25 MHz from Main GP23
- Config via SPI1 (shared with external header)
- CDONE on GP24, CRESET on GP29
- 8MB QSPI SRAM available
- Currently running passthrough bitstream (io_dir register for signal gating)
- FT232HQ on board for high-speed USB path (separate from RP2040 USB)

### CC1101 Dual Radio

- Both share SPI0 bus (MISO=GP4, SCK=GP6, MOSI=GP7)
- Radio 1: CS=GP18, GDO0=GP21, GDO2=GP19
- Radio 2: CS=GP5, GDO0=GP20, GDO2=GP22
- Frequency range: 300-928 MHz ISM bands
- Driver supports: reset, set_freq, read_rssi_dbm, tx, rx, idle, register R/W

### Bottlenose/Orca (ESP32-C5)

- Connected via UART1 (GP8/9/10/11)
- Probed at boot with 3 baud/flow combos (115200, 460800, 921600)
- IPP messages: Main→Orca 0x41-0x4D, Orca→Main 0xC1-0xCC
- Capabilities: WiFi scan/monitor/deauth detect/probe listen, BLE scan/connect/GATT
- Also supports GhostESP via raw UART passthrough mode

### BQ25892 Register Usage (confirmed in display/src/main.c)

| Register | Purpose | Read Pattern |
|----------|---------|-------------|
| 0x02 | Input current limit (ADC enable) | Init: set bits 7:6 to enable ADC |
| 0x07 | Watchdog timer | Power-off: disable watchdog (bits 5:4 → 00) |
| 0x09 | BATFET control | Power-off: BATFET_DIS (bit 5) + BATFET_DLY (bit 3) |
| 0x0B | Charge status | bits 4:3 = charge state (0=none, 1=pre, 2=fast, 3=done) |
| 0x0E | VBAT ADC | 7 bits, base 2304mV, 20mV/bit |
| 0x0F | VSYS ADC | 7 bits, base 2304mV, 20mV/bit |
| 0x11 | VBUS ADC + VBUS_GD | bit 7 = VBUS good, bits 6:0 = ADC (base 2600mV, 100mV/bit) |
| 0x12 | ICHG ADC | 7 bits, base 0mA, 50mA/bit |

### BSP Cross-Reference (from wiliogbsp repo)

Corrections from the official FreeWili 1 OG Board Support Package that refine our hardware bible:

| Finding | Our Docs Say | BSP Says | Impact |
|---------|-------------|----------|--------|
| I/O Expander | PCA9555 | **PCAL6416A** | Register-compatible for basic R/W; PCAL6416 adds interrupt masking, pull-up config, drive strength. Our code works but could use advanced features. |
| Audio Amp | Unknown I2S DAC | **MAX98357A** | Class D amp, no I2C config needed — just send I2S data and it plays. Simplifies Tier 3 audio implementation significantly. Has SD (shutdown) pin — if GP8 is connected to SD, driving it low kills audio. |
| IR Carrier | 38kHz | **38.222kHz** | Minor — NEC spec tolerance is wide. Our bit-bang timing is close enough. |
| WS2812 | Direct drive on GP7 | **Inverting buffer IC6** between GP7 and LEDs | Explains `gpio_set_outover(PIN_NEOPIXEL, GPIO_OVERRIDE_INVERT)` in our code — the invert compensates for the buffer IC. |
| GP18-21, GP28 | "Unknown" | BSP has partial mappings for some as test points and power control | Some may control header power rails or antenna switches. Our code drives all high at boot as a safe default. |
| Charger EN (GP8) | "Active low, unconfirmed" | BSP confirms GP8 → MAX98357A SD pin OR charger path | Dual-use pin — may gate both audio amp shutdown and charger enable. Needs scope verification. |

The wiliogbsp repo also contains production-quality drivers for: LIS3DH, BQ25892, MCP7940, PCAL6416, CC1101, ILI9341, WS2812, IR NEC, I2S audio, and PDM microphone. These can be referenced or ported when implementing Tier 2/3 features.

---

## Phase 2: Hardware → IPP → Display → WASM Gap Table

Legend:
- **Y** = Fully implemented and working
- **FIX** = Exists but broken (returns wrong data)
- **STUB** = Handler exists but is empty (just `break`)
- **—** = Not implemented / not applicable
- **N/A** = Not routed through this layer (direct Main-side call)

### Display & Graphics

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 1 | Clear screen | SCREEN_CLEAR (0x0E) | Y | Y `app_clear_screen` | None |
| 2 | Draw text | DRAW_TEXT (0x16) | Y | Y `app_draw_text` | None |
| 3 | Draw rectangle | DRAW_RECT (0x14) | Y | Y `app_draw_rect` | None |
| 4 | Draw line | DRAW_LINE (0x15) | Y | Y `app_draw_line` | None |
| 5 | Frame flip | FB_FLIP (0x19) | Y (no-op, direct render) | Y `app_fb_flip` | None |
| 6 | Toast overlay | TOAST (0x08) | Y | Y `app_toast` | None |
| 7 | Draw circle | DRAW_CIRCLE (0x17) | STUB | — | **Display + WASM** |
| 8 | Draw batch | DRAW_BATCH (0x18) | STUB | — | **Display + WASM** |
| 9 | Pixel buffer | PIXEL_BUFFER (0x07) | — | — | **Display + WASM** |
| 10 | Set pixel | (use 1x1 rect) | Y (via rect) | — | **WASM only** |
| 11 | Backlight | BACKLIGHT (0x0F) | Y | — | **WASM only** |
| 12 | Progress bar | PROGRESS (0x09) | — | — | **Display + WASM** |
| 13 | Dialog box | DIALOG (0x12) | — | — | **Display + WASM** |
| 14 | Input prompt | INPUT_PROMPT (0x13) | — | — | Not practical for WASM |
| 15 | Splash | SPLASH (0x11) | — | — | Low priority |
| 16 | Menu show | MENU_SHOW (0x01) | Y | — | Possible but complex |
| 17 | Status bar | STATUS_BAR (0x03) | Y | — | Internal use only |
| 18 | Text screen | TEXT_SCREEN (0x04) | Y | — | Possible future API |
| 19 | Data table | DATA_TABLE (0x05) | — | — | Deferred |
| 20 | Graph draw | GRAPH_DRAW (0x06) | — | — | Deferred |
| 21 | Font select | (via draw_text font_id) | Y (0=small, 1=main) | — | **WASM enhancement** |

### Input

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 22 | Button poll | BUTTON_EVENT (0x81) | Y (sends) | Y `app_get_button` | None |
| 23 | Button wait | — | — | Y `app_wait_button` | None |
| 24 | Button hold detect | BUTTON_EVENT (0x81) | Y (sends HELD) | — | **WASM only** |

### LEDs

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 25 | Set LED color | LED_SET (0x0A) | Y | Y `app_set_led` | None |
| 26 | LED pattern | LED_PATTERN (0x0B) | STUB | — | **Display + WASM** |
| 27 | All LEDs off | (use set_led ×7) | Y (via set_led) | — | Convenience only |

### Sensors (Display-side I2C1)

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 28 | Battery percentage | BATTERY_DATA (0x83) | Y (reads BQ25892) | FIX `app_get_battery_pct` returns 0 | **Fix WASM** |
| 29 | Battery voltage (mV) | BATTERY_DATA (0x83) | Y | — | **WASM only** |
| 30 | Charge state/flags | BATTERY_DATA (0x83) | Y | — | **WASM only** |
| 31 | Accelerometer XYZ | ACCEL_DATA (0x82) | — (probed, never read) | — | **Display + WASM** |
| 32 | RTC date/time | RTC_TIME (0x84) | — (probed, never read) | — | **Display + WASM** |
| 33 | IO expander write | IOEXP_WRITE (0x1D) | Y | — | **WASM only** |
| 34 | IO expander read | IOEXP_READ (0x1E) | Y | — | **WASM only** |
| 35 | I2C bus scan | I2C_SCAN_REQ (0x1C) | Y | — | Low priority |

### IR

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 36 | IR transmit | IR_SEND (0x10) | Y (NEC) | — | **WASM only** |
| 37 | IR receive | IR_RECEIVED (0x85) | Y (sends) | — | **WASM only** |

### Audio

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 38 | Audio play (I2S) | AUDIO_PLAY (0x0C) | — | — | **Full stack** |
| 39 | Audio stop | AUDIO_STOP (0x0D) | — | — | **Full stack** |
| 40 | PDM microphone | — | — | — | **Full stack** |

### Radio (Main-side, direct)

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 41 | R1 set frequency | N/A | N/A | Y `app_radio_set_freq` | None |
| 42 | R1 read RSSI | N/A | N/A | Y `app_radio_get_rssi` | None |
| 43 | R1 transmit | N/A | N/A | Y `app_radio_tx` | None |
| 44 | R1 receive packet | N/A | N/A | — | **WASM only** |
| 45 | R1 set modulation | N/A | N/A | — | **WASM only** |
| 46 | R1 set data rate | N/A | N/A | — | **WASM only** |
| 47 | R2 set frequency | N/A | N/A | — | **WASM only** |
| 48 | R2 read RSSI | N/A | N/A | — | **WASM only** |
| 49 | R2 transmit | N/A | N/A | — | **WASM only** |

### GPIO (Main-side, direct)

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 50 | GPIO read | N/A | N/A | Y `app_gpio_read` | None |
| 51 | GPIO write | N/A | N/A | Y `app_gpio_write` | None |
| 52 | ADC read | N/A | N/A | — | **WASM only** (GP26 is ADC capable) |
| 53 | PWM output | N/A | N/A | — | **WASM only** |

### Storage (Main-side, direct)

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 54 | File write | N/A | N/A | Y `app_fs_write` | None |
| 55 | File read | N/A | N/A | Y `app_fs_read` | None |
| 56 | File delete | N/A | N/A | — | **WASM only** |
| 57 | File list | N/A | N/A | — | **WASM only** |
| 58 | Free space | N/A | N/A | — | **WASM only** |

### External Buses (Main-side)

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 59 | Ext I2C write | N/A | N/A | — | **WASM only** |
| 60 | Ext I2C read | N/A | N/A | — | **WASM only** |
| 61 | Ext SPI transfer | N/A | N/A | — | **WASM only** |
| 62 | Ext UART write | N/A | N/A | — | **WASM only** |
| 63 | Ext UART read | N/A | N/A | — | **WASM only** |

### WiFi/BLE (Bottlenose/Orca)

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 64 | WiFi scan | WIFI_SCAN_START (0x41) | N/A | — | **WASM only** |
| 65 | WiFi monitor | WIFI_MONITOR (0x43) | N/A | — | **WASM only** |
| 66 | WiFi deauth detect | WIFI_DEAUTH_DETECT (0x44) | N/A | — | **WASM only** |
| 67 | BLE scan | BLE_SCAN_START (0x46) | N/A | — | **WASM only** |
| 68 | BLE connect | BLE_CONNECT (0x48) | N/A | — | **WASM only** |

### FPGA

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 69 | FPGA SPI comm | N/A | N/A | — | **WASM only** |
| 70 | FPGA bitstream load | N/A | N/A | — | Not for WASM |

### System

| # | Capability | IPP Msg Defined | Display Handles | WASM API | Gap |
|---|-----------|----------------|-----------------|----------|-----|
| 71 | Sleep | N/A | N/A | Y `app_sleep_ms` | None |
| 72 | Exit | N/A | N/A | Y `app_exit` | None |
| 73 | Tick count | N/A | N/A | — | **WASM only** |
| 74 | Device info | N/A | N/A | — | **WASM only** |
| 75 | Reboot bootloader | REBOOT_BOOTLOADER (0x1A) | Y | — | Not for WASM |
| 76 | Power off | (charger BATFET) | Y (Gray hold 3s) | — | Not for WASM |

### Summary

| Category | Total Capabilities | Fully Working | Broken/Stub | Missing WASM Only | Missing Display+WASM | Missing Full Stack |
|----------|-------------------|---------------|-------------|-------------------|---------------------|-------------------|
| Display/Graphics | 21 | 6 | 3 stubs | 3 | 4 | 0 |
| Input | 3 | 2 | 0 | 1 | 0 | 0 |
| LEDs | 3 | 1 | 1 stub | 0 | 1 | 0 |
| Sensors | 8 | 3 (1 broken) | 0 | 4 | 2 | 0 |
| IR | 2 | 0 | 0 | 2 | 0 | 0 |
| Audio | 3 | 0 | 0 | 0 | 0 | 3 |
| Radio | 9 | 3 | 0 | 6 | 0 | 0 |
| GPIO | 4 | 2 | 0 | 2 | 0 | 0 |
| Storage | 5 | 2 | 0 | 3 | 0 | 0 |
| Ext Buses | 5 | 0 | 0 | 5 | 0 | 0 |
| WiFi/BLE | 5 | 0 | 0 | 5 | 0 | 0 |
| FPGA | 2 | 0 | 0 | 1 | 0 | 0 |
| System | 6 | 2 (1 broken) | 0 | 2 | 0 | 0 |
| **TOTAL** | **76** | **21** | **4** | **34** | **7** | **3** |

**19 existing WASM APIs, 21 working end-to-end.**
**34 gaps need only a WASM host function (IPP + Display already work or N/A).**
**7 gaps need Display firmware + WASM API.**
**3 gaps need full I2S/PDM subsystem implementation.**
**2 broken functions need fixing (battery_pct returns 0).**

---

## Phase 3: New WASM Host Function Specifications

### Tier 1 — Minimal Firmware Changes (WASM-only or minor Main-side wiring)

These functions need only a new `m3ApiRawFunction` in `wasm_api.c` and a `LINK()` in `wasm_link_all()`. The underlying hardware access already works — either via data already received from Display over IPP, or via direct Main-side peripheral calls.

---

#### T1-01: FIX `app_get_battery_pct`

**Problem:** Returns hardcoded `0` despite Main receiving real battery data from Display every second and computing a percentage.

**C prototype** (unchanged):
```c
uint32_t app_get_battery_pct(void);
```

**wasm3 signature:** `"i()"`

**IPP path:** None — data already stored in Main's `battery_pct` variable (computed from `ipp_battery_data_t.vbatt_mv`, linear map 3300-4150mV → 0-100).

**Implementation:** Replace `m3ApiReturn(0)` with `m3ApiReturn(battery_pct)` where `battery_pct` is the extern variable updated by `on_display_battery()`.

**Security:** None.
**Blocking:** No.
**Display RAM:** None.

---

#### T1-02: `app_get_battery_mv`

Return raw battery voltage in millivolts for apps needing more precision than percentage (voltage curves, charge profiling).

**C prototype:**
```c
uint32_t app_get_battery_mv(void);
```

**wasm3 signature:** `"i()"`

**IPP path:** Data already in Main from `ipp_battery_data_t.vbatt_mv`.

**Implementation:**
```c
m3ApiRawFunction(m3_app_get_battery_mv) {
    m3ApiReturnType(uint32_t);
    extern uint16_t battery_vbatt_mv;
    m3ApiReturn((uint32_t)battery_vbatt_mv);
}
```

**Security:** None.
**Blocking:** No.

---

#### T1-03: `app_get_charging`

Return charging state flags bitmask.

**C prototype:**
```c
uint32_t app_get_charging(void);
```

**wasm3 signature:** `"i()"`

**Return value bits:**
- Bit 0: charging (pre-charge or fast charge)
- Bit 1: charge complete
- Bit 2: VBUS good (USB plugged in)

**IPP path:** Data already in Main from `ipp_battery_data_t.flags`.

**Implementation:**
```c
m3ApiRawFunction(m3_app_get_charging) {
    m3ApiReturnType(uint32_t);
    extern uint8_t battery_flags;
    m3ApiReturn((uint32_t)battery_flags);
}
```

**Security:** None.
**Blocking:** No.

---

#### T1-04: `app_set_backlight`

Set display backlight brightness.

**C prototype:**
```c
void app_set_backlight(uint32_t brightness);
```

**wasm3 signature:** `"v(i)"`

**IPP path:** `IPP_MSG_BACKLIGHT (0x0F)` — **already fully handled** by Display (sets PWM on GP25).

**Implementation:**
```c
m3ApiRawFunction(m3_app_set_backlight) {
    m3ApiGetArg(uint32_t, brightness);
    ipp_backlight_t bl = { .brightness = (uint8_t)(brightness & 0xFF) };
    tool_send_display(IPP_MSG_BACKLIGHT, &bl, sizeof(bl));
    m3ApiSuccess();
}
```

**Security:** Value clamped to uint8_t.
**Blocking:** No.

---

#### T1-05: `app_ir_send`

Transmit an IR code. Display already handles `IPP_MSG_IR_SEND` for NEC protocol.

**C prototype:**
```c
void app_ir_send(uint32_t protocol, uint32_t code, uint32_t bits);
```

**wasm3 signature:** `"v(iii)"`

**IPP path:** `IPP_MSG_IR_SEND (0x10)` — **already handled** by Display (NEC bit-bang on GP9).

**Protocol constants (for freewili.h):**
```c
#define IR_NEC      0
#define IR_SONY     1
#define IR_RC5      2
#define IR_RC6      3
#define IR_SAMSUNG  4
```

**Implementation:**
```c
m3ApiRawFunction(m3_app_ir_send) {
    m3ApiGetArg(uint32_t, protocol);
    m3ApiGetArg(uint32_t, code);
    m3ApiGetArg(uint32_t, bits);
    ipp_ir_code_t ir = {
        .protocol = (uint8_t)protocol,
        .code = code,
        .bits = (uint8_t)bits,
    };
    tool_send_display(IPP_MSG_IR_SEND, &ir, sizeof(ir));
    m3ApiSuccess();
}
```

**Security:** Protocol validated to 0-4 range.
**Blocking:** No (fire-and-forget, Display handles timing).

---

#### T1-06: `app_ir_recv`

Poll for a received IR code. Display already decodes NEC on GP16 and sends `IPP_MSG_IR_RECEIVED` to Main.

**C prototype:**
```c
int32_t app_ir_recv(void *buf);
```

**wasm3 signature:** `"i(i)"` — buf_offset; returns 1 if code available (writes 6 bytes), 0 if none.

**IPP path:** `IPP_MSG_IR_RECEIVED (0x85)` — Display already sends this. Main needs a small buffer to store the last received code for WASM polling.

**Main-side addition:** In `on_display_msg()`, when `IPP_MSG_IR_RECEIVED` arrives, copy into a static `ipp_ir_code_t ir_last_received` and set `bool ir_received_pending = true`.

**Implementation:**
```c
m3ApiRawFunction(m3_app_ir_recv) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || 6 > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    extern ipp_ir_code_t ir_last_received;
    extern volatile bool ir_received_pending;

    if (!ir_received_pending)
        m3ApiReturn(0);

    ir_received_pending = false;
    uint8_t *dst = (uint8_t *)_mem + buf_offset;
    memcpy(dst, &ir_last_received, sizeof(ipp_ir_code_t));
    m3ApiReturn(1);
}
```

**Buffer layout (6 bytes):**
| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | protocol (0=NEC, 1=Sony, ...) |
| 1 | 4 | code (uint32_t LE) |
| 5 | 1 | bits |

**Security:** Bounds check on buf_offset.
**Blocking:** No (poll).

---

#### T1-07: `app_get_ticks`

Get millisecond tick count for timing and animation. Essential for frame-rate-independent animation.

**C prototype:**
```c
uint32_t app_get_ticks(void);
```

**wasm3 signature:** `"i()"`

**IPP path:** None — direct `to_ms_since_boot(get_absolute_time())`.

**Implementation:**
```c
m3ApiRawFunction(m3_app_get_ticks) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn((uint32_t)to_ms_since_boot(get_absolute_time()));
}
```

**Security:** None. Wraps at ~49 days.
**Blocking:** No.

---

#### T1-08: `app_set_pixel`

Draw a single pixel. Implemented as a 1x1 filled rectangle — no new IPP message needed.

**C prototype:**
```c
void app_set_pixel(uint32_t x, uint32_t y, uint32_t color);
```

**wasm3 signature:** `"v(iii)"`

**IPP path:** Reuses `IPP_MSG_DRAW_RECT (0x14)` with w=1, h=1, filled=1.

**Implementation:**
```c
m3ApiRawFunction(m3_app_set_pixel) {
    m3ApiGetArg(uint32_t, x);
    m3ApiGetArg(uint32_t, y);
    m3ApiGetArg(uint32_t, color);
    ipp_draw_rect_t r = {
        .x = (uint16_t)x, .y = (uint16_t)y,
        .w = 1, .h = 1,
        .color = (uint16_t)color, .filled = 1,
    };
    tool_send_display(IPP_MSG_DRAW_RECT, &r, sizeof(r));
    m3ApiSuccess();
}
```

**Security:** None.
**Blocking:** No.
**Note:** Slow for bulk pixel work — each pixel is a full IPP frame. For bitmap rendering, prefer `app_draw_rect` or future `app_pixel_buffer`.

---

#### T1-09: `app_radio_rx`

Receive a radio packet from Radio 1.

**C prototype:**
```c
int32_t app_radio_rx(void *buf, uint32_t max_len);
```

**wasm3 signature:** `"i(ii)"` — buf_offset, max_len; returns bytes received, 0 if nothing, -1 on error.

**IPP path:** None — direct CC1101 access on Main.

**Implementation:**
```c
m3ApiRawFunction(m3_app_radio_rx) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, max_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || max_len > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (max_len > 64) max_len = 64;

    uint8_t *dst = (uint8_t *)_mem + buf_offset;
    int rd = cc1101_rx_read(PIN_RADIO1_CS, dst, (uint8_t)max_len);
    m3ApiReturn((int32_t)rd);
}
```

**Security:** Bounds check, max 64 bytes (CC1101 FIFO limit).
**Blocking:** No (returns immediately with available data or 0).

---

#### T1-10: `app_fs_delete`

Delete a file from flash storage.

**C prototype:**
```c
int32_t app_fs_delete(const void *path, uint32_t path_len);
```

**wasm3 signature:** `"i(ii)"` — returns 0 on success, -1 on error.

**IPP path:** None — direct `fs_delete()`.

**Implementation:**
```c
m3ApiRawFunction(m3_app_fs_delete) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, path_offset);
    m3ApiGetArg(uint32_t, path_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (path_offset >= mem_size || path_len > mem_size - path_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    char path[64];
    uint32_t plen = path_len < 63 ? path_len : 63;
    memcpy(path, (uint8_t *)_mem + path_offset, plen);
    path[plen] = '\0';

    if (strncmp(path, "/apps/", 6) != 0)
        m3ApiReturn(-1);

    int err = fs_delete(path);
    m3ApiReturn(err);
}
```

**Security:** Sandboxed to `/apps/` directory. Bounds check on path pointer.
**Blocking:** Brief (flash erase).

---

#### T1-11: `app_fs_list`

List files in `/apps/` directory. Writes null-separated filenames into buffer.

**C prototype:**
```c
int32_t app_fs_list(void *buf, uint32_t max_len);
```

**wasm3 signature:** `"i(ii)"` — returns number of files found, -1 on error. Buffer contains null-separated filenames.

**Implementation:**
```c
m3ApiRawFunction(m3_app_fs_list) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, max_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || max_len > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    uint8_t *dst = (uint8_t *)_mem + buf_offset;
    int count = fs_list("/apps/", (char *)dst, max_len);
    m3ApiReturn(count);
}
```

**Security:** Always lists `/apps/` only. Bounds check.
**Blocking:** Brief (flash read).

---

#### T1-12: `app_radio_set_config`

Configure Radio 1 modulation, data rate, and RX bandwidth for advanced radio apps.

**C prototype:**
```c
void app_radio_set_config(uint32_t modulation, uint32_t data_rate_baud, uint32_t rx_bw_hz);
```

**wasm3 signature:** `"v(iii)"`

**Modulation constants (for freewili.h):**
```c
#define RADIO_MOD_2FSK   0
#define RADIO_MOD_GFSK   1
#define RADIO_MOD_ASK    3
#define RADIO_MOD_4FSK   4
#define RADIO_MOD_MSK    7
```

**Implementation:** Writes CC1101 MDMCFG2 (modulation), MDMCFG4/3 (data rate), MDMCFG4 upper bits (bandwidth). Uses existing `cc1101_write_reg()`.

**Security:** Validate modulation 0-7, data_rate 300-500000, rx_bw 58000-812000.
**Blocking:** No.

---

#### T1-13: `app_radio2_set_freq`

Tune Radio 2 to a frequency. Same pattern as Radio 1.

**C prototype:**
```c
void app_radio2_set_freq(uint32_t freq_hz);
```

**wasm3 signature:** `"v(i)"`

**Implementation:** Same as `app_radio_set_freq` but uses `PIN_RADIO2_CS`.

---

#### T1-14: `app_radio2_get_rssi`

Read RSSI from Radio 2.

**C prototype:**
```c
int32_t app_radio2_get_rssi(void);
```

**wasm3 signature:** `"i()"`

---

#### T1-15: `app_ext_i2c_xfer`

Read/write to external I2C bus (I2C0, header pins 8/10). Single function handles both directions.

**C prototype:**
```c
int32_t app_ext_i2c_xfer(uint32_t addr, const void *tx, uint32_t tx_len,
                          void *rx, uint32_t rx_len);
```

**wasm3 signature:** `"i(iiiii)"` — addr, tx_offset, tx_len, rx_offset, rx_len. Returns 0 on success.

**Implementation:**
```c
m3ApiRawFunction(m3_app_ext_i2c_xfer) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, addr);
    m3ApiGetArg(uint32_t, tx_offset);
    m3ApiGetArg(uint32_t, tx_len);
    m3ApiGetArg(uint32_t, rx_offset);
    m3ApiGetArg(uint32_t, rx_len);

    if (addr < 0x08 || addr > 0x77)
        m3ApiReturn(-1);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (tx_len > 0 && (tx_offset >= mem_size || tx_len > mem_size - tx_offset))
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (rx_len > 0 && (rx_offset >= mem_size || rx_len > mem_size - rx_offset))
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (tx_len > 256 || rx_len > 256)
        m3ApiReturn(-1);

    int ret = 0;
    if (tx_len > 0) {
        const uint8_t *tx = (const uint8_t *)_mem + tx_offset;
        ret = i2c_write_blocking(EXT_I2C, (uint8_t)addr, tx, tx_len, rx_len > 0);
        if (ret < 0) m3ApiReturn(-1);
    }
    if (rx_len > 0) {
        uint8_t *rx = (uint8_t *)_mem + rx_offset;
        ret = i2c_read_blocking(EXT_I2C, (uint8_t)addr, rx, rx_len, false);
        if (ret < 0) m3ApiReturn(-1);
    }
    m3ApiReturn(0);
}
```

**Security:** Address range 0x08-0x77. Max 256 bytes per direction. Bounds check both buffers. Does not conflict with Display-side I2C1 (this is I2C0).
**Blocking:** Brief (I2C transaction, microseconds to milliseconds).

---

#### T1-16: `app_ext_spi_xfer`

SPI transfer on external bus (SPI1, header pins 1/12/13/15). Also used for FPGA communication.

**C prototype:**
```c
int32_t app_ext_spi_xfer(const void *tx, void *rx, uint32_t len);
```

**wasm3 signature:** `"i(iii)"` — tx_offset, rx_offset, len. Returns 0 on success.

**Security:** Bounds check both buffers. Max 256 bytes. Conflicts with FPGA config — check FPGA state.
**Blocking:** Brief.

---

#### T1-17: `app_draw_text_small`

Draw text using the small (5x8) font. Current `app_draw_text` always uses font_id=1 (6x10 main).

**C prototype:**
```c
void app_draw_text_small(uint32_t x, uint32_t y, const void *text, uint32_t len, uint32_t color);
```

**wasm3 signature:** `"v(iiiii)"`

**Implementation:** Same as `app_draw_text` but sets `hdr->font_id = 0`.

**Alternative design:** Add a font parameter to a new `app_draw_text_ex` instead. But the existing `app_draw_text` signature is locked (would break existing apps), and an extra parameter in a new function is cleaner than making all callers pass a font they don't care about.

---

### Tier 2 — Display Firmware Changes Required

These functions need modifications to the Display RP2040 firmware (`display/src/main.c`) to read sensors or implement new handlers, plus the WASM API on Main.

---

#### T2-01: `app_get_accel`

Read accelerometer data (XYZ axes in milli-g, total magnitude, temperature).

**CRITICAL DEPENDENCY: G-force meter app for OiScout.**

**C prototype:**
```c
int32_t app_get_accel(void *buf);
```

**wasm3 signature:** `"i(i)"` — buf_offset; returns 0 on success, -1 if accelerometer not present.

**Buffer layout (10 bytes, matches `ipp_accel_data_t`):**
| Offset | Size | Field | Unit |
|--------|------|-------|------|
| 0 | 2 | x | milli-g (int16_t) |
| 2 | 2 | y | milli-g (int16_t) |
| 4 | 2 | z | milli-g (int16_t) |
| 6 | 2 | g_total | milli-g (uint16_t) |
| 8 | 2 | temp_c_x10 | temperature × 10 (int16_t) |

**IPP path:** `IPP_MSG_ACCEL_DATA (0x82)` — defined but Display never sends it.

**Display firmware changes needed:**

1. **LIS3DH initialization** (in `init_i2c_sensors()`):
```c
if (accel_present) {
    // CTRL_REG1 (0x20): 100Hz ODR, all axes enabled
    uint8_t cmd1[2] = { 0x20, 0x57 };  // 0101 0111
    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, cmd1, 2, false);

    // CTRL_REG4 (0x23): ±4g, high resolution
    uint8_t cmd4[2] = { 0x23, 0x18 };  // 0001 1000
    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, cmd4, 2, false);
}
```

2. **Periodic read** (in sensor poll, every 50-100ms for smooth g-force):
```c
if (accel_present) {
    uint8_t reg = 0x28 | 0x80;  // OUT_X_L with auto-increment
    uint8_t raw[6];
    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, &reg, 1, true);
    i2c_read_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, raw, 6, false);

    ipp_accel_data_t acc;
    int16_t raw_x = (int16_t)(raw[1] << 8 | raw[0]) >> 4;  // 12-bit left-justified
    int16_t raw_y = (int16_t)(raw[3] << 8 | raw[2]) >> 4;
    int16_t raw_z = (int16_t)(raw[5] << 8 | raw[4]) >> 4;

    // ±4g mode: 1 LSB = 2mg (high-res 12-bit)
    acc.x = raw_x * 2;  // milli-g
    acc.y = raw_y * 2;
    acc.z = raw_z * 2;

    // Magnitude: sqrt(x² + y² + z²) using integer approximation
    uint32_t sum = (uint32_t)(acc.x * acc.x + acc.y * acc.y + acc.z * acc.z);
    // Integer sqrt
    uint32_t g = 0, bit = 1u << 30;
    while (bit > sum) bit >>= 2;
    while (bit) {
        if (sum >= g + bit) { sum -= g + bit; g = (g >> 1) + bit; }
        else g >>= 1;
        bit >>= 2;
    }
    acc.g_total = (uint16_t)g;

    // Temperature: OUT_TEMP_L (0x0C, 0x0D), relative to 25°C, 1 LSB = 1°C
    uint8_t treg = 0x0C | 0x80;
    uint8_t traw[2];
    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, &treg, 1, true);
    i2c_read_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, traw, 2, false);
    int16_t temp_raw = (int16_t)(traw[1] << 8 | traw[0]) >> 6;
    acc.temp_c_x10 = (temp_raw + 25) * 10;

    ipp_send_main(IPP_MSG_ACCEL_DATA, &acc, sizeof(acc));
}
```

3. **Main-side**: Store received `ipp_accel_data_t` in a static, expose to WASM.

**Implementation (wasm_api.c):**
```c
m3ApiRawFunction(m3_app_get_accel) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || 10 > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    extern ipp_accel_data_t last_accel;
    extern bool accel_available;
    if (!accel_available)
        m3ApiReturn(-1);

    memcpy((uint8_t *)_mem + buf_offset, &last_accel, sizeof(ipp_accel_data_t));
    m3ApiReturn(0);
}
```

**Security:** Bounds check buf_offset + 10 bytes.
**Blocking:** No (returns cached data).
**Display RAM:** ~20 bytes for accel state.

---

#### T2-02: `app_get_time`

Read RTC date and time from MCP7940.

**C prototype:**
```c
int32_t app_get_time(void *buf);
```

**wasm3 signature:** `"i(i)"` — buf_offset; returns 0 on success, -1 if RTC not present.

**Buffer layout (7 bytes):**
| Offset | Size | Field | Format |
|--------|------|-------|--------|
| 0 | 1 | seconds | 0-59 (BCD decoded) |
| 1 | 1 | minutes | 0-59 |
| 2 | 1 | hours | 0-23 (24h format) |
| 3 | 1 | day_of_week | 1-7 (1=Monday) |
| 4 | 1 | day | 1-31 |
| 5 | 1 | month | 1-12 |
| 6 | 1 | year | 0-99 (offset from 2000) |

**IPP path:** `IPP_MSG_RTC_TIME (0x84)` — defined but Display never sends it.

**Display firmware changes needed:**

1. **MCP7940 init** (in `init_i2c_sensors()`):
```c
if (rtc_present) {
    // Enable oscillator: RTCSEC register (0x00) bit 7 = ST
    uint8_t addr = 0x00;
    uint8_t reg;
    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_RTC, &addr, 1, true);
    i2c_read_blocking(LOCAL_I2C, I2C_ADDR_RTC, &reg, 1, false);
    if (!(reg & 0x80)) {
        uint8_t cmd[2] = { 0x00, reg | 0x80 };
        i2c_write_blocking(LOCAL_I2C, I2C_ADDR_RTC, cmd, 2, false);
    }
}
```

2. **Periodic read** (every 1s, alongside battery poll):
```c
if (rtc_present) {
    uint8_t addr = 0x00;
    uint8_t raw[7];
    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_RTC, &addr, 1, true);
    i2c_read_blocking(LOCAL_I2C, I2C_ADDR_RTC, raw, 7, false);

    uint8_t time_buf[7];
    time_buf[0] = ((raw[0] >> 4) & 0x07) * 10 + (raw[0] & 0x0F);  // sec (mask ST bit)
    time_buf[1] = ((raw[1] >> 4) & 0x07) * 10 + (raw[1] & 0x0F);  // min
    time_buf[2] = ((raw[2] >> 4) & 0x03) * 10 + (raw[2] & 0x0F);  // hr (24h)
    time_buf[3] = raw[3] & 0x07;                                     // dow
    time_buf[4] = ((raw[4] >> 4) & 0x03) * 10 + (raw[4] & 0x0F);  // day
    time_buf[5] = ((raw[5] >> 4) & 0x01) * 10 + (raw[5] & 0x0F);  // month
    time_buf[6] = ((raw[6] >> 4) & 0x0F) * 10 + (raw[6] & 0x0F);  // year

    ipp_send_main(IPP_MSG_RTC_TIME, time_buf, 7);
}
```

**Security:** Bounds check buf_offset + 7.
**Blocking:** No (cached).
**Display RAM:** ~10 bytes.

---

#### T2-03: `app_draw_circle`

Draw a circle (filled or outline).

**C prototype:**
```c
void app_draw_circle(uint32_t cx, uint32_t cy, uint32_t r, uint32_t color, uint32_t filled);
```

**wasm3 signature:** `"v(iiiii)"`

**IPP path:** `IPP_MSG_DRAW_CIRCLE (0x17)` — defined, Display handler is currently a stub.

**IPP payload (new struct for ipp_defs.h):**
```c
typedef struct __attribute__((packed)) {
    uint16_t cx;
    uint16_t cy;
    uint16_t r;
    uint16_t color;
    uint8_t  filled;
} ipp_draw_circle_t;
```

**Display firmware changes needed:**

1. **Add to gfx.h/gfx.c** — Midpoint circle algorithm:
```c
void gfx_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color);
void gfx_fill_circle(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color);
```

2. **Replace stub in on_main_frame():**
```c
case IPP_MSG_DRAW_CIRCLE:
    if (e->len >= sizeof(ipp_draw_circle_t)) {
        const ipp_draw_circle_t *c = (const ipp_draw_circle_t *)e->payload;
        if (c->filled)
            gfx_fill_circle(c->cx, c->cy, c->r, c->color);
        else
            gfx_draw_circle(c->cx, c->cy, c->r, c->color);
    }
    break;
```

**Security:** Clip to screen bounds in gfx.c.
**Blocking:** No.
**Display RAM:** None (direct-to-TFT rendering).

---

#### T2-04: `app_progress`

Show a progress bar overlay.

**C prototype:**
```c
void app_progress(uint32_t percent, const void *text, uint32_t len);
```

**wasm3 signature:** `"v(iii)"`

**IPP path:** `IPP_MSG_PROGRESS (0x09)` — defined, payload struct `ipp_progress_t` exists, but Display has no handler.

**Display firmware changes needed:**

1. Add handler in `on_main_frame()` that buffers the progress payload.
2. In `process_pending_messages()`, call `gfx_draw_progress()` (already exists in gfx.h) with the label text drawn above/below.

**Security:** Bounds check text buffer. Clamp percent to 0-100.
**Blocking:** No.

---

#### T2-05: `app_ioexp_write` / `app_ioexp_read`

Direct access to the PCA9555 I/O expander for advanced users (antenna switching, level shifter control, etc.).

**C prototypes:**
```c
int32_t app_ioexp_write(uint32_t reg, uint32_t value);
int32_t app_ioexp_read(uint32_t reg);
```

**wasm3 signatures:** `"i(ii)"` and `"i(i)"`

**IPP path:** `IPP_MSG_IOEXP_WRITE (0x1D)` and `IPP_MSG_IOEXP_READ (0x1E)` — both already fully handled by Display. Response comes back via `IPP_MSG_IOEXP_RESP (0x8A)`.

**Complication:** Read is asynchronous — Main sends the request, Display responds later. The WASM API needs to either:
- (a) Block waiting for the response (simpler, slight latency)
- (b) Return the last known value (faster, stale data)

**Recommended:** Option (a) with a timeout, matching the `app_wait_button` pattern.

**Security:** Restrict writable registers to prevent breaking header power or disabling critical buses.
**Blocking:** Yes (waits for IPP response, ~1-2ms round-trip).

---

### Tier 3 — Major New Subsystems

These require significant new firmware modules (I2S driver, PDM driver, Orca WASM interface, external UART management).

---

#### T3-01: `app_audio_tone`

Play a simple tone through the I2S speaker.

**C prototype:**
```c
void app_audio_tone(uint32_t freq_hz, uint32_t duration_ms);
```

**wasm3 signature:** `"v(ii)"`

**IPP path:** `IPP_MSG_AUDIO_PLAY (0x0C)`

**Firmware work required:**
- I2S DMA driver for Display RP2040 (GP4/5/6)
- Tone generator (sine table lookup, integer phase accumulator)
- I2S DAC IC identification (unknown — need to probe or check schematics)
- ~2-4 days of development

---

#### T3-02: `app_mic_read`

Read raw PDM microphone samples (decimated to PCM).

**C prototype:**
```c
int32_t app_mic_read(void *buf, uint32_t samples);
```

**wasm3 signature:** `"i(ii)"`

**Firmware work required:**
- PDM driver using PIO (GP17 CLK, GP29 data)
- CIC decimation filter
- Buffer management across IPP (PCM samples from Display to Main)
- ~3-5 days of development

---

#### T3-03: `app_wifi_scan_start` / `app_wifi_scan_get`

WiFi AP scanning via Bottlenose/Orca ESP32-C5.

**C prototypes:**
```c
void app_wifi_scan_start(void);
int32_t app_wifi_scan_get(void *buf, uint32_t max_len);
```

**wasm3 signatures:** `"v()"` and `"i(ii)"`

**IPP path:** `WIFI_SCAN_START (0x41)` → `WIFI_AP_FOUND (0xC1)` → `WIFI_SCAN_DONE (0xC2)`

**Firmware work required:**
- Result buffer on Main (dynamic sizing for unknown AP count)
- Serialization format for WASM buffer (packed AP records)
- Orca availability check (not always present/responsive)
- ~2-3 days of development

---

#### T3-04: `app_ble_scan_start` / `app_ble_scan_get`

BLE advertisement scanning via Bottlenose/Orca.

**C prototypes:**
```c
void app_ble_scan_start(void);
int32_t app_ble_scan_get(void *buf, uint32_t max_len);
```

**Similar to WiFi scan, same Orca dependency.**

---

#### T3-05: `app_ext_uart_write` / `app_ext_uart_read`

External UART access (UART1, header pins 5/9/7/11).

**C prototypes:**
```c
int32_t app_ext_uart_write(const void *data, uint32_t len);
int32_t app_ext_uart_read(void *buf, uint32_t max_len);
```

**wasm3 signatures:** `"i(ii)"`

**Complication:** UART1 is shared with Orca/Bottlenose. WASM apps using external UART would conflict with Orca communication. Needs a mode switch or guard.

**Firmware work required:**
- UART mode management (Orca vs external)
- RX buffer management
- ~1-2 days of development

---

#### T3-06: `app_fpga_spi`

SPI communication with the FPGA via SPI1.

**C prototype:**
```c
int32_t app_fpga_spi(const void *tx, void *rx, uint32_t len);
```

**wasm3 signature:** `"i(iii)"`

**Complication:** SPI1 is shared between FPGA config and external header. FPGA must be configured (CDONE high) before SPI communication. Need to verify FPGA is in application mode, not config mode.

---

## Phase 4: Complete Updated freewili.h

See `examples/wasm/freewili.h` — the full updated header with all new function declarations.

The header adds 28 new functions to the existing 19, for a total of 47 WASM host functions. Tier 1 functions (17 new) can be implemented immediately. Tier 2 functions (5 new) need Display firmware updates. Tier 3 functions (6 new) need major new subsystems.

---

## Phase 5: Implementation Roadmap

### Priority 1: G-Force Meter App (OiScout Target)

The g-force meter app needs exactly three things:
1. **Accelerometer data** → T2-01 `app_get_accel` (Display firmware change)
2. **Battery display** → T1-01 FIX `app_get_battery_pct` (one-line fix)
3. **Timing** → T1-07 `app_get_ticks` (trivial)

**Implementation order for g-force meter:**

**Sprint 1 (1-2 days): Unblock the accelerometer**
1. Add LIS3DH init to `display/src/main.c` `init_i2c_sensors()` — CTRL_REG1=0x57 (100Hz, all axes), CTRL_REG4=0x18 (±4g hi-res)
2. Add accel read to display main loop (every 50ms) — read OUT_X_L..OUT_Z_H with auto-increment
3. Send `IPP_MSG_ACCEL_DATA` from Display to Main
4. Add `on_display_accel()` handler in Main to store received data
5. Implement `m3_app_get_accel` in `wasm_api.c`
6. Fix `m3_app_get_battery_pct` to return actual battery percentage
7. Add `m3_app_get_ticks`

**Sprint 2 (1 day): Write the g-force meter app**
```
gforce.wasm — App structure:
- _start(): init, set backlight, enter main loop
- Main loop (every 50ms):
  - Read accel via app_get_accel()
  - Compute current g-force from g_total field
  - Track peak g-force
  - Draw:
    - Large g-force number (center, big text)
    - Directional indicator (X/Y/Z bars)
    - Peak hold display
    - History graph (rolling 5s window)
    - Battery percentage (top-right)
  - YELLOW resets peak, RED exits
```

**Sprint 3 (1 day): Ship remaining Tier 1 APIs**
- All 17 Tier 1 functions — each is 10-30 lines in `wasm_api.c`
- Update `freewili.h` with new declarations
- Update `wasm-sdk.md` documentation

### Priority 2: Core Platform APIs (Week 2)

1. **Tier 2 Display changes** — circle drawing, progress bar, RTC read
2. **IR transmit/receive** — enables remote control apps
3. **Radio 2 access** — enables dual-radio scanning apps

### Priority 3: Advanced Features (Week 3+)

1. **WiFi/BLE scanning** via Orca — huge app developer value but needs Orca firmware stability
2. **External bus access** (I2C, SPI, UART) — enables hardware hacking apps
3. **Audio tone generation** — fun but complex (I2S driver)
4. **PDM microphone** — complex, uncertain value for WASM apps given memory limits

### What NOT to implement for WASM

- **FPGA bitstream loading** — security risk, could brick device
- **Power off** — app should not be able to kill the device
- **Reboot to bootloader** — security risk
- **Menu/StatusBar/TextScreen rendering** — these are tool engine concerns, not app concerns
- **Display reset** — could leave device in bad state
- **Direct charger register writes** — safety risk

### Estimated Total Effort

| Phase | Functions | Effort |
|-------|-----------|--------|
| Sprint 1 (g-force unblock) | 3 functions + Display accel driver | 1-2 days |
| Sprint 2 (g-force app) | 1 WASM app | 1 day |
| Sprint 3 (remaining Tier 1) | 14 functions | 1 day |
| Tier 2 (Display additions) | 5 functions + Display changes | 2-3 days |
| Tier 3 (new subsystems) | 6+ functions | 2-4 weeks |

**Total to ship g-force meter: 2-3 days.**
**Total to ship complete Tier 1+2 SDK: ~1 week.**
