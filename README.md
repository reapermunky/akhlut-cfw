<p align="center">
  <img src="installer/akhlut_logo.jpg" alt="Akhlut CFW" width="300">
</p>

<h1 align="center">Akhlut CFW</h1>
<p align="center"><em>Custom firmware for the FreeWili 1 OG.</em></p>

<p align="center">
  <img src="https://img.shields.io/badge/target-FreeWili%201%20OG-00d4ff?style=flat-square" alt="Target">
  <img src="https://img.shields.io/badge/license-MIT-green?style=flat-square" alt="License">
  <img src="https://img.shields.io/badge/platform-RP2040-blue?style=flat-square" alt="Platform">
</p>

---

Akhlut CFW replaces the entire stock FreeWili firmware stack across both RP2040 processors. Akhlut turns the hardware into a full-featured multi-tool for radio, WiFi/BLE, bus analysis, GPIO, IR, and more.

## Features

### Sub-GHz Scan & Capture (315/433/868/915 MHz)
Dual CC1101 radios scan across ISM bands with configurable bandwidth. Live RSSI bar graph, peak detection, and signal capture.

### WiFi & BLE via GhostESP (37 commands)
Full GhostESP control through the Orca header - WiFi scanning, deauth, handshake capture, beacon spam, BLE spam (Apple/Google/Samsung/Microsoft), GATT scanning, wardrive, and more. Cursor-based AP selection with one-button targeting.

### I2C / UART Bus Analysis
Scan external I2C devices, internal sensor bus, or bridge USB to UART at 115200/460800/921600 baud for protocol analysis.

### GPIO Monitor
Real-time read and toggle of external header GPIO pins (GP24, GP25, GP27).

### IR Receive & Replay
Capture up to 8 IR codes (NEC, Sony, RC5, RC6, Samsung protocols), then replay them on demand.

### WASM App Engine
Write your own apps in C, compile to WebAssembly, and run them on the device. Full SDK with display, input, radio, GPIO, LED, and storage APIs. See [Writing WASM Apps](#writing-wasm-apps).

### Persistent Settings
Backlight brightness, LED control, and auto-sleep - saved to flash, restored on boot.

### One-Click Installer
GUI installer handles everything: enter bootloader, flash both processors, upload WASM apps. Also manages apps on the device (list, add, delete).

## Install

### Requirements

- Python 3.8+
- FreeWili 1 OG with battery connected
- USB cable

### Steps

```bash
pip install pyserial freewili
```

1. Download the [latest release](../../releases/latest)
2. Plug in your FreeWili (battery must be connected)
3. Run the installer:
   ```bash
   cd installer
   python installer.py
   ```
4. Choose the right button for your situation:
   - **Install from Stock** - first time installing Akhlut on stock FreeWili firmware
   - **Update Akhlut** - already running Akhlut, installing a newer version
   - **Restore Stock FW** - go back to the original FreeWili firmware

The installer flashes both the Main and Display RP2040 processors and uploads any `.wasm` apps from the `apps/` folder.

### Managing WASM Apps

The installer includes a built-in app manager. After installing the firmware:

1. Click **Refresh** in the WASM Apps section to see installed apps
2. **Add .wasm** to upload new apps from your computer
3. Select an app and click **Delete** to remove it

## Restore to Stock

Run the installer and click **Restore Stock FW**. Stock firmware files are included in `stock/`. These are large (16-17 MB each) so the flash takes 5-10 minutes - do not unplug the device during this process.

## Building from Source

### Prerequisites

- [Pico SDK](https://github.com/raspberrypi/pico-sdk) with `PICO_SDK_PATH` set
- `arm-none-eabi-gcc` toolchain
- CMake 3.13+

### Build Main Firmware

```bash
cd main
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
mkdir build && cd build
cmake ..
make -j$(nproc)
# Output: freewili_main.uf2
```

### Build Display Firmware

```bash
cd display
cp $PICO_SDK_PATH/external/pico_sdk_import.cmake .
mkdir build && cd build
cmake ..
make -j$(nproc)
# Output: freewili_display.uf2
```

### Flash Order

Flash Display first, then Main. Main controls Display's reset line, so flashing Main first can leave Display in an inconsistent state.

**Manual flash:** 1200-baud DTR touch on the Main COM port puts the Main RP2040 into bootloader mode. Copy the `.uf2` to the `RPI-RP2` drive that appears. To flash Display, send `B` over serial to Main - it commands Display into bootloader mode via IPP.

## Writing WASM Apps

Apps are plain C compiled to WebAssembly. No stdlib required - the firmware provides everything through host functions.

```c
#include "freewili.h"

void _start(void) {
    app_clear_screen();
    draw_str(100, 110, "Hello FreeWili!", COLOR_CYAN);
    app_fb_flip();
    app_wait_button();
    app_exit();
}
```

Build with clang:

```bash
clang --target=wasm32 -nostdlib -O2 \
      -Wl,--no-entry -Wl,--export=_start \
      -Wl,--initial-memory=65536 \
      -o hello.wasm hello.c
```

Upload via the installer or copy to `/apps/` on the device filesystem.

See [docs/wasm-sdk.md](docs/wasm-sdk.md) for the full API reference and [examples/wasm/](examples/wasm/) for complete example apps.

## Project Structure

```
akhlut/
├── main/                  Main RP2040 firmware (tool engine, radios, FPGA)
├── display/               Display RP2040 firmware (TFT, buttons, sensors)
├── common/                Shared IPP protocol (both processors)
├── firmware/              Pre-built UF2 binaries
├── installer/             One-click installer GUI
├── apps/                  WASM apps to install on device
├── examples/wasm/         Example WASM app source + SDK header
├── docs/                  Hardware reference and SDK docs
└── stock/                 Stock firmware for restore
```

## Hardware Reference

See [docs/hardware-bible.md](docs/hardware-bible.md) for the complete hardware reference including:

- Full GPIO maps for both RP2040s
- IC identification and I2C addresses
- 20-pin header pinout
- USB COM port mapping
- IPP protocol specification
- Boot sequences
- Power management (BQ25892)
- FPGA pass-through configuration
- CC1101 radio driver details

## Architecture

Three processors, one UX:

| Processor | Chip | Role |
|-----------|------|------|
| **Main** | RP2040 | Tool engine, radios, FPGA, GPIO, serial console |
| **Display** | RP2040 | TFT rendering, buttons, LEDs, sensors, IR |
| **Orca** | ESP32-C6 | WiFi + BLE (GhostESP firmware) |

Main and Display communicate over **IPP** (Inter-Processor Protocol) - a binary framing protocol over UART with CRC-16 integrity. Main talks to Orca using plain text serial (GhostESP CLI) through a raw UART buffer path.

## Credits

- **[OpenKeiko](https://github.com/openkeiko/openkeiko-docs)** - Hardware reverse engineering and initial pin mapping
- **[GhostESP](https://github.com/Spooks4576/ghost_esp)** - WiFi/BLE firmware for the ESP32-C6 Orca module
- **[wasm3](https://github.com/wasm3/wasm3)** - WebAssembly interpreter that powers the WASM app engine

## License

[MIT](LICENSE)
