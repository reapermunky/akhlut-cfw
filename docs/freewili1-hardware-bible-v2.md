# FreeWili 1 (RP2040) — Hardware Bible v2

**Compiled by Akhlut CFW**
*Consolidated from vendor docs, GitHub repos, Python API source, code examples, and datasheets. August 2026.*

> The canonical reference for the FreeWili 1. Everything in one place. Updated with Python API internals, filesystem layout, USB identifiers, framing protocol, and full peripheral mapping.

---

## 1. Device Identity & Architecture

The FreeWili 1 is a dual-RP2040 embedded security/development tool made by **Intrepid Control Systems, Inc.** (USB Vendor ID `093c`). The company's core business is automotive network tools (ValueCAN, neoVI) — the FreeWili is their embedded hacking/dev device.

### USB Identity

| Component | VID:PID | Description |
|-----------|---------|-------------|
| Intrepid Hub | `093c:*` | Internal USB hub (Main + Display) |
| FT232H (FPGA) | `0403:6014` | FTDI high-speed USB for FPGA path |
| RP2040 CDC | `2e8a:000a` | Serial CDC interfaces (Main + Display) |
| RP2040 UF2 | `2e8a:0003` | BOOTSEL mode for firmware flashing |

Linux udev rules (required for non-root access):
```
SUBSYSTEM=="usb", ATTRS{idVendor}=="093c", GROUP="users", MODE="0666"
KERNEL=="ttyUSB?", ATTRS{idVendor}=="093c", GROUP="users", MODE="0666"
KERNEL=="ttyACM?", ATTRS{idVendor}=="093c", GROUP="users", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6014", GROUP="users", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="000a", GROUP="users", MODE="0666"
SUBSYSTEM=="usb", ATTR{idVendor}=="2e8a", ATTR{idProduct}=="0003", GROUP="users", MODE="0666"
```

### Three Processor Targets

The Python API defines `FreeWiliProcessorType` with three targets:

| Type | Chip | Role | Serial Interface |
|------|------|------|------------------|
| **Main** | RP2040 | I/O, protocols, radio, scripting, FPGA control | `main_serial` |
| **Display** | RP2040 | Screen, buttons, LEDs, audio, UI rendering | `display_serial` |
| **FTDI** | FT232HQ | High-speed USB for FPGA data path | Via libftdi |

The `FreeWili` Python object exposes `.main`, `.display`, `.fpga`, `.hub`, `.main_serial`, `.display_serial`, `.mass_storage`, `.standalone`, `.unique_id`, `.usb_devices`.

### Core Architecture

```
USB-C ──► Internal USB Hub (093c) ──┬── RP2040 MAIN (CDC ttyACM) ──► FPGA ──► GPIO Header
                                    ├── RP2040 DISPLAY (CDC ttyACM) ──► LCD + Buttons + LEDs + Audio
                                    └── FT232HQ (0403:6014) ──► FPGA high-speed data path
```

---

## 2. GPIO Header — Complete Pin Map

**20-pin header** (2×10). Pin 1 marked on PCB.

**CRITICAL**: Pin 4 (V PINS IN) **MUST** have voltage for GPIO to function.

### Voltage Setup
- **5V**: Jumper pins 2↔4
- **3.3V**: Jumper pins 4↔6  
- **Custom**: Apply 1.1V–5.5V directly to pin 4

### Pin Assignments

| Pin | Name | Default Dir | Protocol Role | RP2040 GPIO | Notes |
|-----|------|------------|---------------|-------------|-------|
| 1 | CS | Output | SPI Chip Select | — | — |
| 2 | 5V | Power | 5V supply | — | Jumper to pin 4 |
| 3 | GPIO_24 | Configurable | General purpose | GP24 | — |
| 4 | V PINS IN | Power | **Voltage ref** | — | **REQUIRED** |
| 5 | UART RX | Input | UART Receive | — | — |
| 6 | 3.3V | Power | 3.3V supply | — | Jumper to pin 4 |
| 7 | UART CTS | Input | Clear-To-Send | — | HW flow ctrl |
| 8 | I2C SCL | Bidir | I2C Clock | — | PCA9517, 10K pulls |
| 9 | UART TX | Output | UART Transmit | — | — |
| 10 | I2C SDA | Bidir | I2C Data | — | PCA9517, 10K pulls |
| 11 | UART RTS | Output | Request-To-Send | — | HW flow ctrl |
| 12 | SPI MISO | Input | Master-In-Slave-Out | — | — |
| 13 | SPI MOSI | Output | Master-Out-Slave-In | — | — |
| 14 | GPIO_25 | Configurable | General purpose | GP25 | Used in Python examples |
| 15 | SPI SCLK | Output | SPI Clock | — | — |
| 16 | GPIO_26 | Configurable | General purpose | GP26 | FPGA can set dir |
| 17 | GPIO_27 | Configurable | General purpose | GP27 | FPGA can set dir |
| 18 | GPIO_28 | Configurable | General purpose | GP28 | — |
| 19 | GND | Ground | — | — | — |
| 20 | GND | Ground | — | — | — |

### GPIO Operations (via Python API)

`IOMenuCommand` enum defines available operations per pin:
- **High** — drive pin high
- **Low** — drive pin low  
- **Toggle** — toggle state
- **Get** — read current state
- **Pwm** — PWM output (params: frequency_kHz, duty_percent)
- **Stream** — continuous streaming mode

Example: `fw.set_io(25, IOMenuCommand.Pwm, 10, 50)` — 10kHz, 50% duty on GPIO 25

**High-speed IO mode**: `fw.toggle_high_speed_io(True/False)` — separate toggle for high-speed GPIO operations.

### Electrical Specs

| Parameter | Value |
|-----------|-------|
| IO voltage range | 1.1V – 5.5V (via pin 4) |
| Drive @ 3V | 24 mA/pin |
| Drive @ 5V | 32 mA/pin |
| Level shifter | sn74lxc1t45 (per signal pin) |
| I2C buffer | PCA9517 (0.9V–5.5V, 10KΩ SW pulls) |

**Signal path**: Header → sn74lxc1t45 → iCE40UP5K FPGA → RP2040 MAIN

---

## 3. Protocol Configuration

### SPI
- **Pins**: CS(1), MISO(12), MOSI(13), SCLK(15)
- **Freq**: Up to 70 MHz (default 5 MHz)
- **Modes**: CPOL/CPHA programmable
- **Full duplex**: Yes
- **API**: `fw.read_write_spi_data(bytes)` → returns rx bytes

### I2C
- **Pins**: SCL(8), SDA(10)
- **Freq**: 100kHz (std), 400kHz (fast), 1MHz (fast+)
- **Pull-ups**: 10KΩ built-in, SW enable/disable
- **API**: `fw.poll_i2c()`, `fw.read_i2c(addr, reg, len)`, `fw.write_i2c(addr, reg, data)`

### UART
- **Pins**: RX(5), CTS(7), TX(9), RTS(11)
- **Baud**: Up to 8,000,000 bps (default 115,200)
- **Format**: 8-N-1 default, configurable
- **Flow ctrl**: HW RTS/CTS
- **Limitation**: v54 firmware had 22-byte message limit (use 16 to be safe)
- **API**: `fw.write_uart(bytes)`, event-based RX via `enable_uart_events()`

### MDIO (Ethernet PHY Debug)
- Clause 22: `fw.mdio_read_22()`, `fw.mdio_write_22()`
- Clause 45: `fw.mdio_read_45()`, `fw.mdio_write_45()`
- EMU mode: `fw.mdio_read_emu()`, `fw.mdio_write_emu()`
- SFP: `fw.mdio_read_sfp()`, `fw.mdio_write_sfp()`
- Read-modify-write variants for all types
- Polling: `fw.mdio_poll_phy()`, `fw.mdio_poll_sfp()`

### CAN Bus (via Neptune Orca module)
- 2 channels (CAN0, CAN1)
- TX/RX with arbitration ID, extended frames
- CAN FD data support
- RX filters, periodic TX
- **API**: `fw.can_transmit()`, `fw.can_enable_streaming()`, `fw.can_set_rx_filter()`

All protocols run on **dedicated pin sets** and can operate **simultaneously**.

---

## 4. FPGA Subsystem — iCE40UP5K

### Components

| Part | IC | Function |
|------|----|----------|
| FPGA | iCE40UP5K | IO routing, logic analyzer, custom state machines |
| SRAM | APS6404L-3SQR-ZR | 8 MB Serial SRAM |
| USB | FT232HQ | High-speed USB, FT1248 mode |

### Default Bitstream
- **Pass-through**: Routes RP2040 GPIO → breakout transparently
- **Logic analyzer**: Sigrok-compatible, 31.25 MHz sample rate, RLE encoding, 32 KB FIFO (SPRAM)
- **IO direction control**: For SPI, UART, GPIO_26, GPIO_27 (requires IO_CONFIG_ENABLE assertion)

### Named FPGA Configurations
The firmware stores pre-built FPGA configs by name. The Python API loads them with:
```python
fw.load_fpga_from_file("i2c")  # loads I2C-optimized FPGA config
```
Known named configs: `i2c` (others likely exist in `/fpga/` directory on Main processor).

### Reprogramming
- SRAM-based — unlimited reprogramming
- OTP default loaded at power-up
- RP2040 reprograms via SPI Slave Configuration Interface (Lattice FPGA-TN-02001)
- Open toolchain: **IceStorm / Yosys / nextpnr**

---

## 5. Radio Subsystem — Dual CC1101

Two TI CC1101 sub-GHz transceivers. Two SMA antenna connectors.

### Frequency Bands

| Band | Range | Common Uses |
|------|-------|-------------|
| 1 | 300–348 MHz | US keyless entry (315 MHz), some TPMS |
| 2 | 387–464 MHz | EU keyless entry, garage doors, weather stations, ISM 433 MHz |
| 3 | 779–928 MHz | EU ISM 868, US ISM 915, LoRa, Z-Wave, TPMS |

### Radio API
- `fw.select_radio(n)` — select Radio 1 or 2
- `fw.write_radio(data)` — transmit data
- `fw.transmit_radio_subfile(filename)` — transmit from .sub file (Flipper-compatible format)
- `fw.set_radio_event_rssi_threshold(value)` — set RSSI threshold for event generation
- `fw.set_radio_event_sample_window(value)` — set sample window
- `fw.enable_radio_events(True)` — subscribe to Radio1/Radio2 events
- File format compatible with "existing market devices" (Flipper Zero .sub files)

---

## 6. Buttons & Input

### Physical Buttons (5 buttons, color-coded)

| Button | Color | Python Attr |
|--------|-------|-------------|
| 1 | **Gray** | `ButtonData.gray` |
| 2 | **Yellow** | `ButtonData.yellow` |
| 3 | **Green** | `ButtonData.green` |
| 4 | **Blue** | `ButtonData.blue` |
| 5 | **Red** | `ButtonData.red` |

- **API**: `fw.read_all_buttons()` returns dict of button states (0=released, 1=pressed)
- **Events**: `fw.enable_button_events(True, poll_ms)` for async notification
- Gray button is power on/off (hold RED to power off per docs)

### Accelerometer
- 3-axis with temperature sensor
- **Data**: x, y, z, g (total acceleration), temp_c, temp_f
- **Events**: `fw.enable_accel_events(True, poll_ms)` — configurable polling rate
- Model not specified in docs (likely LIS3DH or similar)

---

## 7. Display, Audio & LEDs

### Display
- 320 × 240 color LCD (driven by DISPLAY RP2040)
- **API**: `fw.show_gui_image(image)`, `fw.show_text_display(text)`
- Custom GUI panels supported via WASM and host binary API
- `fw.reset_display()` to reset

### Audio
- Digital speaker + microphone
- **Sample rate**: 8 kHz, 16-bit mono
- **Playback**: 
  - Tones: `fw.play_audio_tone(freq_hz, duration_sec, amplitude)`
  - Speech: `fw.play_audio_number_as_speech(number)` — TTS for numbers
  - Assets: `fw.play_audio_asset(index)` — 100+ built-in ROM sounds
  - Files: `fw.play_audio_file(filename)`
- **Recording**: `fw.record_audio()` + `fw.enable_audio_events(True)` — streams 16-bit PCM via events
- **System sounds**: `fw.set_system_sounds(True/False)`

### Built-in Audio Assets (selected highlights)
DefCon, HackMe, ScanMeHarder, AllYourBases, DaveICantDoThat, FearIsTheMindKiller, IllBeBack, WarWarNeverChanges, GodLike, EndOfLine, I2C, SPI, UART, GPIO, Welcome, etc. (~100 total)

### LEDs
- **7 RGB LEDs** (NeoPixel/WS2812)
- **API**: `fw.set_board_leds(led_num, r, g, b)` — led_num 0-6

### IR
- **Transmit**: `fw.send_ir(bytes)` — NEC protocol (Roku example: `bytes([0xBE, 0xEF, 0x00, 0xFF])`)
- **Receive**: Via events: `fw.enable_ir_events(True)` → `IRData.value` as bytes
- Note: Standard unit has IrDA sensor (not fully standard IR remote compatible). Add TSOP38238 to SAO for standard remote reception.

---

## 8. Battery & Power

Battery telemetry via `fw.enable_battery_events(True)`:

| Field | Type | Description |
|-------|------|-------------|
| `vbatt` | float | Battery voltage (mV) |
| `vbus` | float | USB bus voltage (mV) |
| `vsys` | float | System voltage (mV) |
| `ichg` | int | Charge current (mA) |
| `charging` | bool | Currently charging |
| `charge_complete` | bool | Charge complete |

- 1000 mAh LiPo with integrated charger
- USB-C for power and data
- Battery management IC not documented

---

## 9. Filesystem

Both processors have independent FAT filesystems (16MB × 2 flash, ~22MB usable total).

### Main Processor (`FreeWiliProcessorType.Main`)
```
/
├── settings.txt          # Configuration (saved with 's' command)
├── testfw.bin            # Test firmware
├── wili.jpeg             # Device image
├── scripts/              # WASM (.wasm) and ZoomIO (.zio) scripts
├── radio/                # Radio capture/transmit files (.sub format)
└── fpga/                 # FPGA bitstream files (named configs)
```

### Display Processor (`FreeWiliProcessorType.Display`)
```
/
├── settings.txt          # Display configuration
├── sounds/               # Audio files
└── images/               # Image files (.fwi format)
```

### File Operations
- `fw.send_file(path, target_name, processor)` — auto-routes by extension
- `fw.get_file(source, dest, processor)` — download from device with progress callback
- `fw.list_current_directory(processor)` → `FileSystemContents`
- `fw.change_directory(path, processor)`
- `fw.create_directory(name)`, `fw.create_blank_file(name)`
- `fw.remove_directory_or_file(path)`, `fw.move_directory_or_file(src, dst)`
- `fw.format_filesystem()` — **destructive**
- `fw.run_script(name)` — execute WASM/ZoomIO script on device
- `fw.stop_script()` — halt running script

### Image Format
`.fwi` — proprietary image format. Convert with `fwi-convert` CLI tool:
```bash
fwi-convert input.png output.fwi
```
Python: `from freewili.image import convert`

---

## 10. Communication Protocol (Framing)

The Python API communicates via a structured binary framing protocol over USB serial.

### ResponseFrame Structure
- `seq_number` — sequence number for request/response matching
- `timestamp` — device timestamp (convertible via `timestamp_as_datetime()`)
- `success` — bool indicating command success
- `rf_type` — `ResponseFrameType`: Standard | Event | Invalid
- `rf_type_data` — type-specific metadata
- `response` — response payload (string)
- `response_as_bytes()` — raw bytes

### Event System
Event-driven architecture for async data. Callback signature:
```python
def handler(event_type: EventType, frame: ResponseFrame, data: EventDataType) -> None:
```

**EventTypes**: Accel, Audio, Battery, Button, CANRX0, CANRX1, CANTX0, CANTX1, File, GPIO, IR, NFC, Radio1, Radio2, UART1

The device has a `GPIO_MAP` in `types.py` that maps RP2040 GPIO numbers to named functions — this is the missing link between header pin numbers and RP2040 GPIO addresses.

---

## 11. Scripting Engines

### WASM (WiliWasm)
- Languages: C/C++, Rust, Zig, TinyGo → compile to .wasm
- Full device control via WASM API
- Upload: `fw.send_file("app.wasm", None, None)` → auto-routes to `/scripts/`
- Execute: `fw.run_script("app.wasm")`
- Stop: `fw.stop_script()`

### ZoomIO
- Extension: `.zio`
- **Sub-nanosecond GPIO precision**
- Compiles to native ARM assembly at load time
- Runs on RP2040 Core 1 in dedicated 4K scratch RAM
- Single-cycle IO for deterministic timing
- Example:
```
setio(27, 1);
delay(2000);    // nanoseconds
setio(27, 0);
delay(1000);
```

### Host-Side Tools
| Tool | Function |
|------|----------|
| `freewili` (PyPI, MIT, v0.0.51) | Full device control API, Python 3.10+ |
| `fwi-serial` CLI | Interactive serial console |
| `fwi-convert` CLI | PNG/JPG → .fwi image conversion |
| `fwcom_v16.exe` | Windows serial console |

---

## 12. Serial Console Menu

Full text menu accessible via any serial terminal:
```
h) SPI          l) I2C          g) UART
f) FPGA         r) Radio 1      m) Radio 2
y) Radio Frequency Analyzer
d) GPIO Directions
a) NeoPixel Light Show
t) RTC          w) Wifi         c) BT
j) Websocket Server
o) Orca Setup   k) Default Script
i) Default FPGA
s) Save Settings as Startup
n) Software Reset
b) Software Reset To Bootloader
z) Set settings to default
```

Settings stored in `/settings.txt`. Saved with `s` option. Changes apply when exiting settings menu.

---

## 13. Expansion — Orca Modules

Plug into GPIO header. Configured via serial menu `o) Orca Setup`:
- `0) Off` / `1) BottleNose` / `2) WhaleTail` / `3) WILEye`

| Orca | Function | Interface |
|------|----------|-----------|
| **Bottlenose** (ESP32-C6) | WiFi, BLE, Websocket server | UART (auto-configured) |
| **WhaleTail** | Unknown (listed in setup) | UART |
| **WILEye** (ESP32-P4-EYE) | Camera, video, photos | UART 5 Mbps + CTS/RTS |
| **Maestro Debug** | USB debugger, test points, QWIIC, SD | Physical debug interface |
| **Neptune** | CAN bus (2 channels) | SPI/UART |
| **Custom** | DIY via Orca dev kit | GPIO header |

---

## 14. Key IC Reference

| Component | Part Number | Notes |
|-----------|-------------|-------|
| Main CPU | RP2040 | Dual Cortex-M0+, 133 MHz, 264KB SRAM |
| Display CPU | RP2040 | Same |
| FPGA | iCE40UP5K | 5280 LUTs, 128KB SPRAM, 120KB BRAM |
| FPGA SRAM | APS6404L-3SQR-ZR | 8MB QSPI SRAM |
| FTDI USB | FT232HQ | FT1248 mode, high-speed |
| Level shifter | sn74lxc1t45 | Per-pin, 1.1–5.5V |
| I2C buffer | PCA9517 | 0.9–5.5V, SW 10K pulls |
| Radio (×2) | CC1101 (TI) | Same chip as Flipper Zero sub-GHz |
| USB Hub | Unknown | 3-port (2 FS + 1 HS) |

---

## 15. Firmware

| Processor | Latest Known | File |
|-----------|-------------|------|
| Main | v92 | `FreeWiliMainV92.uf2` |
| Display | v67 | `FreeWiliDisplayV67.uf2` |

Repo: `github.com/freewili/freewili-firmware` (binary drops only, no source)

**Also available**: `defcon32_badge.uf2`, `defcon33_badge.uf2` — badge firmwares

### Flashing
- Serial command `b` → "Software Reset To Bootloader" (no physical button press needed)
- Or Python: `fw.reset_to_uf2_bootloader()`
- Then drag-drop .uf2 to mounted USB mass storage

---

## 16. Source Repos & Resources

| Resource | URL |
|----------|-----|
| Firmware binaries | `github.com/freewili/freewili-firmware` |
| Web docs (Docusaurus) | `github.com/freewili/FreeWili_WebDocs` |
| Python API source | `github.com/freewili/freewili-python` |
| Python API docs | `freewili.github.io/freewili-python/` |
| PyPI package | `pypi.org/project/freewili/` |
| Community docs (artktec) | `github.com/artktec/freewili_docs` |
| Official docs (mixed v1+v2) | `docs.freewili.com` |
| FreeWili v2 BSP | `github.com/freewili/wilibsp` |
| Sigrok fork | (referenced but URL not published) |
| Official site (v2-focused) | `freewili.com` |
| Discord | `FREE-WILi Discord` (linked from docs footer) |

---

## 17. Known Gaps & Next Steps

### Gaps to Fill
1. **RP2040 GPIO → FPGA → Header pin mapping** — The Python `GPIO_MAP` in `types.py` has this data. Need to extract it (requires reading the source or probing the device).
2. **CC1101 SPI bus wiring** — Which RP2040 SPI instance, which CS/MISO/MOSI/SCLK pins.
3. **MAIN ↔ DISPLAY inter-processor link** — Likely UART but undocumented.
4. **FPGA bitstream Verilog source** — Referenced but not published.
5. **LCD controller IC** — Likely ILI9341 or ST7789 (320×240 is both).
6. **Battery management IC** — Telemetry exists, charger IC unknown.
7. **Accelerometer model** — Has temp sensor, 3-axis + G-force. Likely LIS3DH or similar.
8. **Full schematic** — Not public.
9. **WASM API reference** — API exists, docs thin.
10. **Flash memory layout** — Partition boundaries, bootloader reserved regions.
11. **OpenKeiko project** — Chris mentioned it as a community effort; no public repos found yet. May be Discord-only or pre-launch.

### Immediate Actions When Device Is Available
1. Connect via Python API: `pip install freewili` → `FreeWili.find_first()`
2. Run `get_app_info()` for both processors to confirm firmware versions
3. Explore filesystem on both processors (example code above)
4. Dump `settings.txt` from both processors
5. Extract `GPIO_MAP` from `freewili.types` Python module
6. Probe I2C bus with `poll_i2c()` to identify onboard peripherals
7. Try `load_fpga_from_file()` with different names to discover available configs
8. Enable all event types and capture baseline sensor data

---

*This document will be updated as we reverse-engineer the gaps and build our tooling.*

---

## APPENDIX A — OpenKeiko Reverse-Engineered Pinout (github.com/openkeiko/openkeiko-docs)

*This data fills nearly every gap in Section 17. Recovered via static firmware analysis, hardware probing, and validated with replacement MicroPython firmware.*

### Main RP2040 — Complete GPIO Map

| GPIO | Function | Connected To | Evidence |
|------|----------|-------------|----------|
| 0 | UART0 TX | Display RP2040 link | static analysis |
| 1 | UART0 RX | Display RP2040 link | static analysis |
| 2 | UART0 CTS | Display RP2040 link | static analysis |
| 3 | UART0 RTS | Display RP2040 link | static analysis |
| 4 | SPI0 MISO | Shared CC1101 radio bus | static analysis |
| 5 | SPI0 CS | **Radio 2** | static analysis |
| 6 | SPI0 SCK | Shared CC1101 radio bus | static analysis |
| 7 | SPI0 MOSI | Shared CC1101 radio bus | static analysis |
| 8 | UART1 TX | External header pin 9 | documented + static |
| 9 | UART1 RX | External header pin 5 | documented + static |
| 10 | UART1 CTS | External header pin 7 | documented + static |
| 11 | UART1 RTS | External header pin 11 | documented + static |
| 12 | SPI1 MISO | Header pin 12 + FPGA path | documented + static |
| 13 | SPI1 CS | Header pin 1 + FPGA config CS | documented + static |
| 14 | SPI1 SCK | Header pin 15 + FPGA config CLK | documented + static |
| 15 | SPI1 MOSI | Header pin 13 + FPGA path | documented + static |
| 16 | I2C0 SDA | External header pin 10 | documented + static |
| 17 | I2C0 SCL | External header pin 8 | documented + static |
| 18 | SPI0 CS | **Radio 1** | static analysis |
| 19 | GDO2 | CC1101 Radio 1 | static analysis |
| 20 | GDO0 | CC1101 Radio 2 | static analysis |
| 21 | GDO0 | CC1101 Radio 1 | static analysis |
| 22 | GDO2 | CC1101 Radio 2 | static analysis |
| 23 | FPGA clock | 31.25 MHz clock input | documented + validated |
| 24 | FPGA CDONE | Config status | static + validated |
| 25 | General output | Header pin 17 (status LED) | documented + static |
| 26 | General input | Header pin 14 | documented + static |
| 27 | General output | Header pin 3 | documented + static |
| 28 | Display RUN/reset | Display RP2040 RUN (active low) | static + physically observed |
| 29 | FPGA CRESET_B | FPGA config reset | static + validated |

### Main-Side Bus Summary

| Bus | Pins | Role |
|-----|------|------|
| UART0 | TX=0, RX=1, CTS=2, RTS=3 | Main↔Display link @ **8 Mbaud** |
| SPI0 | MOSI=7, MISO=4, SCK=6 | Shared radio bus; CS18=R1, CS5=R2 |
| UART1 | TX=8, RX=9, CTS=10, RTS=11 | External header |
| SPI1 | MOSI=15, MISO=12, SCK=14, CS=13 | External header + FPGA config |
| I2C0 | SDA=16, SCL=17 | External header |

### Display RP2040 — Complete GPIO Map

| GPIO | Function | Connected To | Evidence |
|------|----------|-------------|----------|
| 0-3 | UART0 | Main RP2040 link (crossed PCB) | static analysis |
| 4 | I2S data | Speaker | static + validated |
| 5 | I2S BCLK | Speaker | static analysis |
| 6 | I2S WS/LR | Speaker | static analysis |
| 7 | NeoPixel data | 7-element WS2812 LED chain | static + physically observed |
| 8 | Charger enable? | BQ25892 (active low) | unresolved |
| 9 | IR transmit | IR output | static analysis |
| 10 | SPI1 SCK | TFT display | static + validated |
| 11 | SPI1 MOSI | TFT display | static + validated |
| 12 | Data/Command | TFT display | static + validated |
| 13 | SPI1 CS | TFT display | static + validated |
| 14 | **Gray button** | Button event 0 | static analysis |
| 15 | **Yellow button** | Button event 1 | static analysis |
| 16 | IR receive | IR input | static analysis |
| 17 | PDM mic clock | Microphone | static analysis |
| 18-21 | Unresolved | Unknown | — |
| 22 | **Green button** | Button event 2 | static analysis |
| 23 | **Blue button** | Button event 3 (active low, boot) | static + physically observed |
| 24 | **Red button** | Button event 4 | static analysis |
| 25 | Backlight PWM | TFT backlight | static + validated |
| 26 | I2C1 SDA | Local sensor/control bus | static + validated |
| 27 | I2C1 SCL | Local sensor/control bus | static + validated |
| 28 | Unresolved | Unknown | — |
| 29 | PDM mic data | Microphone | static analysis |

### Display-Side I2C Peripherals (I2C1 @ 400 kHz)

| Address | Device | Function |
|---------|--------|----------|
| 0x19 | **LIS3DH** | 3-axis accelerometer + temperature |
| 0x21 | **PCA9555/TCA9555** | I/O expander: port 0 = header direction, port 1 = radio filter select |
| 0x6B | **BQ25892** | Charger / power-path controller |
| 0x6F | **MCP7940** | Real-time clock |

### External Header — Corrected Pin Map

| Pin | Function | Main GPIO | Front End |
|-----|----------|-----------|-----------|
| 1 | SPI1 CS output | GP13 | SN74LXC1T45 |
| 2 | 5V output | — | Power |
| 3 | General output | GP27 | SN74LXC1T45 |
| 4 | V_REF input (1.1-5.5V) | — | **REQUIRED** |
| 5 | UART1 RX | GP9 | SN74LXC1T45 |
| 6 | 3.3V output | — | Power |
| 7 | UART1 CTS | GP10 | SN74LXC1T45 |
| 8 | I2C0 SCL | GP17 | PCA9517 |
| 9 | UART1 TX | GP8 | SN74LXC1T45 |
| 10 | I2C0 SDA | GP16 | PCA9517 |
| 11 | UART1 RTS | GP11 | SN74LXC1T45 |
| 12 | SPI1 MISO | GP12 | SN74LXC1T45 |
| 13 | SPI1 MOSI | GP15 | SN74LXC1T45 |
| 14 | General input | GP26 | SN74LXC1T45 |
| 15 | SPI1 SCK | GP14 | SN74LXC1T45 |
| 16 | **SWCLK** | SWCLK | **DIRECT** (no level shifter) |
| 17 | General output / LED | GP25 | SN74LXC1T45 |
| 18 | **SWDIO** | SWDIO | **DIRECT** (no level shifter) |
| 19 | GND | — | Ground |
| 20 | GND | — | Ground |

**Pins 16/18 are SWD debug — you can attach a debug probe directly through the header.**

### FPGA iCE40UP5K Pin Assignments

| Function | RP2040 side | External side |
|----------|-------------|---------------|
| SPI MISO | pin 14 | pin 25 |
| SPI MOSI | pin 17 | pin 23 |
| SPI CLK | pin 15 | pin 27 |
| SPI CS | pin 16 | pin 26 |
| UART TX | pin 6 | pin 28 |
| UART RX | pin 9 | pin 31 |
| UART CTS | pin 10 | pin 32 |
| UART RTS | pin 11 | pin 34 |
| GPIO26 (RP/ext/dir) | 19 / 36 / 35 | — |
| GPIO27 (RP/ext/dir) | 18 / 41 / 40 | — |
| FPGA clock | pin 37 | — |
| I2C SDA/SCL | 43 / 38 | — |
| IO config enable | pin 39 | — |
| SRAM CLK/CS | 42 / 3 | — |
| SRAM IO0-3 | 12 / 21 / 13 / 20 | — |
| FTDI IO0-3 | 46 / 47 / 44 / 48 | — |
| FTDI SCLK/SS/MISO | 45 / 2 / 4 | — |

### OpenKeiko Resources

| Resource | URL |
|----------|-----|
| Documentation repo | github.com/openkeiko/openkeiko-docs |
| MicroPython firmware | github.com/openkeiko/openkeiko-fw |
| Hardware overview | docs/hardware-overview.md |
| System architecture | docs/system-architecture.md |
| Display controller | docs/display-controller.md |
| Power system | docs/power-system.md |
| Recovery/flashing | docs/recovery-and-flashing.md |

### Remaining Unresolved Items (per OpenKeiko)
1. Red button → Main RP2040 BOOTSEL strap continuity test
2. Display GP8 → BQ25892 charger enable confirmation
3. Exact I/O expander suffix (PCA9555 vs TCA9555)
4. I/O expander port 1 bit 5 and external net "W8"
5. Display GP18-21 and GP28 connections
6. Board revision differences
