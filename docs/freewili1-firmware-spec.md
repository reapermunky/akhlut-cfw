# FreeWili 1 Custom Firmware Platform — Design Specification

**Akhlut CFW**
*Draft v1 — August 2026*

> Ground-up custom firmware for all three processors. A menu-driven, tool-based platform where every hardware capability is accessible from the device, no laptop required.

---

## 1. Design Goals

1. **Every capability reachable from 5 buttons.** Sub-GHz, WiFi, BLE, bus analysis, GPIO — all navigable from the device with zero host dependency.
2. **Tool architecture, not monolith.** Each capability is a self-contained tool module with a clean lifecycle. Adding a new tool means writing one module, not touching the core.
3. **Three processors, one UX.** Main, Display, and Bottlenose are invisible to the user. The device feels like one thing.
4. **Captures are portable.** Saved data uses open formats (Flipper .sub for sub-GHz, pcap for packets, CSV/binary for bus captures). No proprietary lock-in.
5. **Host mode is a bonus, not a crutch.** USB serial gives power users a CLI and scripting API, but the device is fully functional standalone.

---

## 2. System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                         USER                                │
│                    5 Buttons + Screen                       │
└──────────────────────┬──────────────────────────────────────┘
                       │
┌──────────────────────▼──────────────────────────────────────┐
│                  DISPLAY RP2040                             │
│                                                             │
│  ┌─────────┐  ┌──────────┐  ┌──────┐  ┌────┐  ┌────────┐  │
│  │ UI      │  │ Button   │  │ LED  │  │ IR │  │ Audio  │  │
│  │ Renderer│  │ Scanner  │  │ Ctrl │  │TX/RX│ │ Engine │  │
│  └────┬────┘  └────┬─────┘  └──┬───┘  └──┬─┘  └───┬────┘  │
│       │            │           │         │         │        │
│       └────────────┴───────────┴─────────┴─────────┘        │
│                           │                                 │
│  Local I2C bus:  LIS3DH │ PCA9555 │ BQ25892 │ MCP7940      │
│                           │                                 │
└───────────────────────────┼─────────────────────────────────┘
              UART0 @ 8Mbaud│(GPIO 0-3, HW flow ctrl)
                            │
┌───────────────────────────▼─────────────────────────────────┐
│                    MAIN RP2040                               │
│                    (The Brain)                               │
│                                                              │
│  ┌──────────────────────────────────────────────────┐        │
│  │              TOOL ENGINE                          │        │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐            │        │
│  │  │ SubGHz  │ │ WiFi*   │ │ BLE*    │            │        │
│  │  │ Scanner │ │ Recon   │ │ Scanner │  ...       │        │
│  │  └─────────┘ └─────────┘ └─────────┘            │        │
│  └──────────────────────────────────────────────────┘        │
│                                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ Radio    │  │ FPGA     │  │ GPIO /   │  │ Host     │    │
│  │ Driver   │  │ Manager  │  │ Bus HAL  │  │ Serial   │    │
│  │ (CC1101) │  │(iCE40UP) │  │(SPI/I2C/ │  │ CLI      │    │
│  │          │  │          │  │ UART)    │  │          │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
│                                                              │
│  SPI0: Radio bus (CS18=R1, CS5=R2)                          │
│  SPI1: Header + FPGA config                                 │
│  UART1: Header (external)                                    │
│  I2C0: Header (external)                                     │
│  GP23: FPGA clk | GP24: CDONE | GP29: CRESET_B              │
│  GP28: Display RUN/reset                                     │
│                                                              │
└───────────────────────────┬──────────────────────────────────┘
              UART1 via Orca│header (auto-configured)
                            │
┌───────────────────────────▼──────────────────────────────────┐
│                 BOTTLENOSE (ESP32-C5)                         │
│                                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                   │
│  │ WiFi     │  │ BLE      │  │ Packet   │                   │
│  │ Scanner  │  │ Scanner  │  │ Capture  │                   │
│  │          │  │          │  │          │                   │
│  └──────────┘  └──────────┘  └──────────┘                   │
│                                                              │
│  * WiFi and BLE tools run HERE but are                       │
│    controlled by Main and rendered by Display                │
└──────────────────────────────────────────────────────────────┘
```

### Processor Responsibilities

| Processor | Owns | Does NOT Do |
|-----------|------|-------------|
| **Main RP2040** | Tool execution, radio control, FPGA, external GPIO/bus, host serial, Bottlenose coordination | Screen rendering, button reading, audio, LEDs |
| **Display RP2040** | UI rendering, button input, LEDs, audio, IR, accelerometer, RTC, power monitoring | Tool logic, radio, GPIO, protocol analysis |
| **Bottlenose ESP32-C5** | WiFi scanning, BLE scanning, packet operations | Anything else — it's a slave to Main |

### The Rule
**Main decides what to show. Display decides how to show it.** Main sends structured UI commands ("show menu with these items", "draw this status bar", "render this data table"). Display handles layout, fonts, scrolling, animation. This separation means Display firmware rarely needs to change when tools are added.

### Main RP2040 Dual-Core Split

The RP2040 has two Cortex-M0+ cores. We use both:

| Core | Responsibilities | Why |
|------|-----------------|-----|
| **Core 0** | Tool engine, IPP protocol (both UARTs), radio drivers, FPGA control, host CLI, all event handling | Must never stall. Handles all real-time I/O. |
| **Core 1** | Flash filesystem I/O (save captures, load settings, read/write files) | Flash writes block XIP for 10–50+ ms. Isolating them to Core 1 prevents UART FIFO overflows on Core 0. |

**Critical constraint**: The Main RP2040 executes code from external flash via XIP (Execute-In-Place). When Core 1 writes to flash, it disables the XIP cache and locks the flash bus. If Core 0 needs to fetch an instruction from flash during this window, it stalls until the write completes — dropping UART data.

**Mitigation**:
```c
// All UART ISRs and IPP receive buffers MUST be in RAM, not flash.
// The __not_in_flash_func() attribute keeps them executable during flash writes.

void __not_in_flash_func(uart0_irq_handler)(void) {
    // IPP frame receive — Display link
    // This runs from SRAM even when flash is locked
}

void __not_in_flash_func(uart1_irq_handler)(void) {
    // IPP frame receive — Bottlenose link
}

// IPP ring buffers are statically allocated in .bss (SRAM)
static uint8_t __attribute__((section(".uninitialized_data")))
    ipp_rx_buf_display[IPP_RX_BUF_SIZE];
static uint8_t __attribute__((section(".uninitialized_data")))
    ipp_rx_buf_orca[IPP_RX_BUF_SIZE];
```

**Inter-core communication**: Use the RP2040's hardware mailbox (SIO FIFO) and a shared ring buffer in SRAM for Core 0 → Core 1 file write requests. Core 1 posts completion status back via the same FIFO.

The same dual-core split applies to the **Display RP2040**: Core 0 handles buttons, sensors, UART, and event dispatch. Core 1 handles TFT SPI writes (DMA-driven framebuffer flush) so display updates don't block input handling.

---

## 3. Inter-Processor Protocol (IPP)

### Frame Format (Main ↔ Display, Main ↔ Bottlenose)

```
┌──────┬──────┬──────┬────────┬─────────────┬──────┐
│ SYNC │ SEQ  │ TYPE │ LENGTH │   PAYLOAD   │ CRC  │
│ 0xF1 │ u8   │ u8   │ u16-LE │  0-4096 B   │ u16  │
└──────┴──────┴──────┴────────┴─────────────┴──────┘
```

| Field | Size | Description |
|-------|------|-------------|
| SYNC | 1 byte | `0xF1` — frame start marker |
| SEQ | 1 byte | Sequence number (0-255, wraps). Responses echo the request's SEQ. |
| TYPE | 1 byte | Message type (see tables below) |
| LENGTH | 2 bytes | Payload length (little-endian, 0-4096) |
| PAYLOAD | 0-4096 bytes | Type-specific data |
| CRC | 2 bytes | CRC-16/CCITT over TYPE+LENGTH+PAYLOAD |

**Transport**: UART, 8N1, hardware flow control (CTS/RTS).
- Main ↔ Display: 8 Mbaud (UART0, GPIO 0-3)
- Main ↔ Bottlenose: 921600 baud (UART1 via Orca header, configurable up to 3 Mbaud)

### Bandwidth Constraints & Mitigations

**Main ↔ Display (8 Mbaud ≈ 800 KB/s)**:
A full 320×240 frame at 16-bit color is ~153 KB = ~190ms to transmit = ~5 FPS max. The PIXEL_BUFFER command (0x07) must therefore only be used for **partial region updates** (dirty rectangles). For structured data (graphs, grids, text), Main sends **drawing commands** (see 0x14–0x19 below) instead of rasterized pixels. Display owns the rendering. This is why the high-level UI primitives exist — they move the rendering work to the processor that has direct access to the framebuffer.

**Main ↔ Bottlenose (921600 baud ≈ 92 KB/s)**:
A busy WiFi channel in monitor mode generates 1–5 MB/s of raw traffic. The UART will saturate instantly if the Bottlenose acts as a dumb pipe. **The Bottlenose must filter aggressively before sending.** It is never a raw packet relay. All WiFi monitor commands (0x43, 0x44, 0x45) include filter parameters. The Bottlenose applies BPF-style filtering on-chip (frame type, MAC whitelist, RSSI floor) and only sends matching frames over the IPP. Raw pcap capture mode, if needed, writes directly to the ESP32's flash or streams over the Bottlenose's own USB (if accessible via Maestro debug Orca) — never through the Main UART.

### Message Types — Main → Display

| Type | Name | Payload | Description |
|------|------|---------|-------------|
| 0x01 | MENU_SHOW | MenuDef (JSON or packed) | Show a menu with title + items |
| 0x02 | MENU_UPDATE | Item index + new state | Update a single menu item |
| 0x03 | STATUS_BAR | StatusDef | Update top status bar (battery, radio, WiFi icons) |
| 0x04 | TEXT_SCREEN | Lines + scroll pos | Show a text view (logs, captures) |
| 0x05 | DATA_TABLE | Columns + rows | Render a data table (scan results, bus data) |
| 0x06 | GRAPH_DRAW | Type + data points | Draw a graph (spectrum, signal strength) |
| 0x07 | PIXEL_BUFFER | Region + pixel data | Raw pixel write (for custom tool UIs) |
| 0x08 | TOAST | Text + duration_ms | Show a temporary notification overlay |
| 0x09 | PROGRESS | Percent + label | Show/update a progress bar |
| 0x0A | LED_SET | LED index + RGB | Set individual LED color |
| 0x0B | LED_PATTERN | Pattern ID + params | Run a predefined LED animation |
| 0x0C | AUDIO_PLAY | Asset index or tone def | Play audio |
| 0x0D | AUDIO_STOP | — | Stop audio |
| 0x0E | SCREEN_CLEAR | — | Clear display |
| 0x0F | BACKLIGHT | Brightness (0-255) | Set display brightness |
| 0x10 | IR_SEND | Protocol + code | Transmit IR code |
| 0x11 | SPLASH | Image data or ID | Show splash/boot screen |
| 0x12 | DIALOG | Title + text + buttons | Show a confirmation/choice dialog |
| 0x13 | INPUT_PROMPT | Label + input type | Request text/number input |
| 0x14 | DRAW_RECT | x, y, w, h, color, filled | Draw rectangle (grid lines, bars, backgrounds) |
| 0x15 | DRAW_LINE | x0, y0, x1, y1, color | Draw line (graph axes, separators) |
| 0x16 | DRAW_TEXT | x, y, font_id, color, string | Render text at position (labels, values) |
| 0x17 | DRAW_CIRCLE | cx, cy, r, color, filled | Draw circle/dot (data points, indicators) |
| 0x18 | DRAW_BATCH | Count + array of draw ops | Batch multiple draw commands in one frame |
| 0x19 | FRAMEBUFFER_FLIP | Region (or 0 = full) | Commit buffered draw ops to screen (double-buffer) |

*Note: Drawing primitives (0x14–0x19) are for custom tool UIs that need pixel-level control without the overhead of sending rasterized pixel data. The high-level widgets (MENU_SHOW, DATA_TABLE, GRAPH_DRAW) remain the preferred path for standard views. Draw ops accumulate in Display's back-buffer until FRAMEBUFFER_FLIP commits them — this prevents partial-frame tearing.*

### Message Types — Display → Main

| Type | Name | Payload | Description |
|------|------|---------|-------------|
| 0x81 | BUTTON_EVENT | Button ID + state (press/release/hold) | Button input |
| 0x82 | ACCEL_DATA | x, y, z, g, temp | Accelerometer reading |
| 0x83 | BATTERY_DATA | vbatt, vbus, vsys, ichg, charging | Power status |
| 0x84 | RTC_TIME | Timestamp | Current time from MCP7940 |
| 0x85 | IR_RECEIVED | Protocol + raw data | IR code received |
| 0x86 | AUDIO_DONE | — | Playback completed |
| 0x87 | DIALOG_RESULT | Selected option index | User's dialog choice |
| 0x88 | RENDER_READY | — | Display ready for next frame |

### Message Types — Main → Bottlenose

| Type | Name | Payload | Description |
|------|------|---------|-------------|
| 0x41 | WIFI_SCAN_START | Band (2.4/5/both) + params | Start WiFi AP scan |
| 0x42 | WIFI_SCAN_STOP | — | Stop scanning |
| 0x43 | WIFI_MONITOR | Channel + BPF filter (frame type mask, MAC whitelist, RSSI floor) | Enter monitor mode (filtered — never raw relay) |
| 0x44 | WIFI_DEAUTH_DETECT | Channel(s) | Listen for deauth frames |
| 0x45 | WIFI_PROBE_LISTEN | — | Capture probe requests |
| 0x46 | BLE_SCAN_START | Filter params | Start BLE advertisement scan |
| 0x47 | BLE_SCAN_STOP | — | Stop BLE scan |
| 0x48 | BLE_CONNECT | MAC + addr type | Connect to BLE device |
| 0x49 | BLE_GATT_DISCOVER | — | Enumerate GATT services |
| 0x4A | BLE_DISCONNECT | — | Disconnect |
| 0x4B | PING | — | Heartbeat / presence check |
| 0x4C | VERSION_GET | — | Get Bottlenose firmware version |
| 0x4D | RESET | — | Soft reset Bottlenose |

### Message Types — Bottlenose → Main

| Type | Name | Payload | Description |
|------|------|---------|-------------|
| 0xC1 | WIFI_AP_FOUND | SSID, BSSID, ch, RSSI, auth, band | Single AP result |
| 0xC2 | WIFI_SCAN_DONE | Count | Scan completed |
| 0xC3 | WIFI_FRAME | Type + raw 802.11 frame | Captured frame (monitor mode) |
| 0xC4 | WIFI_DEAUTH_ALERT | BSSID + source + count | Deauth detected |
| 0xC5 | WIFI_PROBE | MAC + SSID + RSSI | Probe request captured |
| 0xC6 | BLE_ADV_FOUND | MAC, type, name, RSSI, adv data | BLE advertisement |
| 0xC7 | BLE_SCAN_DONE | Count | BLE scan completed |
| 0xC8 | BLE_GATT_RESULT | Services/chars/descriptors | GATT enumeration result |
| 0xC9 | BLE_DATA | Handle + data | BLE notification/read data |
| 0xCA | PONG | Uptime + free heap | Heartbeat response |
| 0xCB | VERSION | Version string | Firmware version |
| 0xCC | ERROR | Code + message | Error report |

---

## 4. Boot Sequence

```
POWER ON
  │
  ├── Main RP2040 boots first
  │   ├── Toggle GP28 LOW→HIGH to HARD RESET Display RP2040
  │   │   └── (Guarantees both processors start from known state.
  │   │        Prevents IPP desync if Main rebooted from watchdog
  │   │        while Display stayed powered.)
  │   ├── Init UART0 → wait for Display RENDER_READY
  │   │
  │   ├── *** SAFE MODE CHECK ***
  │   │   ├── Wait 100ms, then query Display for button state
  │   │   ├── If RED button held during power-on:
  │   │   │   ├── Ignore /settings.json entirely → load hardcoded defaults
  │   │   │   │   (brightness=200, volume=50, default freqs, etc.)
  │   │   │   ├── Skip Bottlenose init
  │   │   │   ├── Send TOAST("SAFE MODE") to Display
  │   │   │   ├── Show main menu with settings tool highlighted
  │   │   │   └── User can fix settings, save, and reboot normally
  │   │   └── If no button held: proceed with normal boot
  │   │
  │   ├── Load settings from /settings.json
  │   │   └── If parse fails → fallback to hardcoded defaults + TOAST("Settings reset")
  │   ├── Init SPI0 → probe both CC1101 radios (CS GP18, GP5)
  │   │   ├── Read PARTNUM (0x30) and VERSION (0x31) registers
  │   │   └── Report radio status to Display (STATUS_BAR)
  │   ├── Init FPGA → assert CRESET_B (GP29), load default bitstream, wait for CDONE (GP24)
  │   ├── Init I2C0, SPI1, UART1 (external header — idle until tool needs them)
  │   ├── Detect Orca → probe UART1 with PING, set orca_present flag
  │   │   ├── If Bottlenose: send VERSION_GET, report to Display
  │   │   └── If none: skip, WiFi/BLE tools show "No Orca" on selection
  │   ├── Init host serial (USB CDC) → CLI ready
  │   ├── Send MENU_SHOW (main menu) to Display
  │   └── Enter main event loop
  │
  ├── Display RP2040 boots (after GP28 reset release from Main)
  │   ├── Init TFT (SPI1, backlight PWM on GP25)
  │   ├── Show boot splash (from local flash, no Main dependency)
  │   ├── Init I2C1 bus → probe LIS3DH, BQ25892, MCP7940, PCA9555
  │   ├── Read RTC time
  │   ├── Read battery state
  │   ├── Init button scanner (GP14,15,22,23,24)
  │   ├── Init LED chain (GP7, 7× WS2812)
  │   ├── Init audio (I2S on GP4-6)
  │   ├── Send RENDER_READY on UART0 (SEQ always starts at 0x00)
  │   └── Enter event loop (scan buttons, sensors → send to Main)
  │
  └── Bottlenose ESP32-C5 (if present, and not safe mode)
      ├── Boot from internal flash
      ├── Init UART → wait for PING from Main
      ├── Respond with PONG (uptime, heap)
      └── Enter command loop (wait for Main instructions)
```

### Boot Time Target
Splash visible within **200ms** of power-on. Main menu interactive within **1 second**.

### Safe Mode (Red button held at power-on)
Bypasses /settings.json and Orca init entirely. Prevents bricked-UI scenarios from:
- Corrupted settings file
- Display brightness saved as 0
- Broken Orca firmware causing boot hang
- Any setting that makes the device unusable

Safe mode loads hardcoded defaults and drops directly into the main menu with Settings highlighted so the user can fix and re-save.

---

## 5. UI Framework

### Button Mapping

| Button | Color | Short Press | Long Press (>500ms) | Context |
|--------|-------|-------------|---------------------|---------|
| ▲ | Yellow | Navigate up / scroll up | Fast scroll | Universal |
| ▼ | Blue | Navigate down / scroll down | Fast scroll | Universal |
| ● | Green | Select / confirm / enter | — | Universal |
| ◄ | Red | Back / cancel / exit tool | Force exit to main menu | Universal |
| ◉ | Gray | Context action (tool-specific) | Power off (hold 3s) | Tool-dependent |

**Gray is the wildcard.** Each tool defines what Gray does — toggle capture, switch view, cycle mode, etc. The current Gray action is shown in a hint bar at the bottom of the screen.

### Screen Layout (320×240)

```
┌──────────────────────────────────────┐
│ ▌STATUS BAR (16px)                   │  ← Battery, radio, WiFi/BLE icons, time
├──────────────────────────────────────┤
│                                      │
│                                      │
│         CONTENT AREA                 │  ← 320×208 — menus, data, graphs
│         (320 × 208px)                │
│                                      │
│                                      │
├──────────────────────────────────────┤
│ ▌HINT BAR (16px)  [Gray: Capture]    │  ← Shows current Gray button action
└──────────────────────────────────────┘
```

### UI Primitives (Display firmware provides these)

| Primitive | Description |
|-----------|-------------|
| **Menu** | Scrollable list with icon + label per item, highlight bar, wrapping |
| **TextScreen** | Scrollable text log, auto-scroll option, line limit |
| **DataTable** | Columnar data with headers, sortable, scrollable |
| **Graph** | Line graph, bar chart, or spectrum waterfall |
| **Dialog** | Modal with title, text, and 2-3 button options |
| **Toast** | Temporary overlay notification (1-3 seconds) |
| **Progress** | Bar with percentage and label |
| **InputPrompt** | Numeric or hex input via up/down buttons |
| **SplitView** | Top/bottom split for dual-pane displays (e.g., spectrum + log) |
| **PixelCanvas** | Raw framebuffer region for custom tool rendering |

### Color Palette

Embedded display, high contrast, readable in bright environments:

| Use | Color | Hex |
|-----|-------|-----|
| Background | Near-black | `#0A0A0A` |
| Primary text | Off-white | `#E8E8E8` |
| Highlight / selected | Electric blue | `#00AAFF` |
| Active / capturing | Signal green | `#00FF66` |
| Warning / attention | Amber | `#FFAA00` |
| Error / danger | Red | `#FF3333` |
| Muted / disabled | Dark gray | `#444444` |
| Status bar BG | Charcoal | `#1A1A1A` |

---

## 6. Tool Architecture

### Tool Module Interface

Every tool is a struct that implements this interface:

```c
typedef struct {
    const char *name;           // "Sub-GHz Scanner"
    const char *slug;           // "subghz_scan"
    const char *icon;           // Icon identifier for menu
    const char *description;    // One-line description

    // Lifecycle
    void (*init)(ToolContext *ctx);       // Called once when tool is selected
    void (*start)(ToolContext *ctx);      // Called to begin operation
    void (*stop)(ToolContext *ctx);       // Called to pause/stop
    void (*destroy)(ToolContext *ctx);    // Called when exiting tool

    // Event handlers
    void (*on_button)(ToolContext *ctx, ButtonEvent *evt);   // Button input
    void (*on_timer)(ToolContext *ctx, uint32_t tick_ms);    // Periodic tick
    void (*on_data)(ToolContext *ctx, DataEvent *evt);       // Incoming data (radio, bus, BLE, etc.)

    // Rendering
    void (*render)(ToolContext *ctx, UIFrame *frame);  // Build UI frame for Display

    // Metadata
    uint32_t requires;          // Bitmask: REQUIRE_RADIO1, REQUIRE_RADIO2,
                                //          REQUIRE_ORCA, REQUIRE_FPGA,
                                //          REQUIRE_HEADER_POWER, etc.
    uint32_t tick_interval_ms;  // How often on_timer fires (0 = never)
} Tool;
```

### ToolContext

```c
typedef struct {
    // Hardware access (HAL)
    RadioHAL *radio1;
    RadioHAL *radio2;
    FPGAHAL *fpga;
    BusHAL *spi;        // External SPI1
    BusHAL *i2c;        // External I2C0
    BusHAL *uart;       // External UART1
    GpioHAL *gpio;      // External GPIO pins
    OrcaHAL *orca;      // Bottlenose (NULL if not present)

    // UI
    UIHandle *ui;        // Send frames to Display

    // Storage
    FileHandle *storage; // Read/write captures to flash

    // State
    void *tool_data;     // Pointer into static tool_state_pool (see below)
    bool running;        // True when actively capturing/scanning
} ToolContext;
```

### Memory Management — No Heap Allocation

The RP2040 has 264KB of SRAM and no MMU. Repeated malloc/free of differently-sized tool state blocks **will** fragment the heap and eventually cause a hard crash. Since only one tool runs at a time, we eliminate the heap entirely:

```c
// tool_state_pool.h — statically allocated, zero-fragmentation tool memory

#include "tools/subghz_scanner.h"
#include "tools/wifi_scan.h"
#include "tools/ble_scanner.h"
#include "tools/i2c_scanner.h"
// ... all tool headers

// Union of all possible tool state structs.
// Only one is active at a time. Compiler allocates
// sizeof(largest member) once at compile time.
typedef union {
    SubGhzScannerState  subghz_scanner;
    SubGhzReplayState   subghz_replay;
    SubGhzAnalyzerState subghz_analyzer;
    WiFiScanState       wifi_scan;
    WiFiMonitorState    wifi_monitor;
    WiFiDeauthState     wifi_deauth;
    BLEScannerState     ble_scanner;
    BLEGattState        ble_gatt;
    I2CScannerState     i2c_scanner;
    SPIMonitorState     spi_monitor;
    UARTBridgeState     uart_bridge;
    LogicAnalyzerState  logic_analyzer;
    GPIOControlState    gpio_control;
    IRRemoteState       ir_remote;
    SettingsState       settings;
} ToolStatePool;

// Single static instance — lives for the lifetime of the firmware
static ToolStatePool tool_state_pool;

// In tool_engine.c, when launching a tool:
// ctx->tool_data = &tool_state_pool;
// memset(&tool_state_pool, 0, sizeof(ToolStatePool));  // Zero before init
// tool->init(ctx);
```

**Rules**:
- `tool_data` always points to `&tool_state_pool` — never to malloc'd memory
- `memset` to zero before calling `tool->init()` — clean slate
- Each tool casts `ctx->tool_data` to its own state type
- Maximum tool state size is bounded at compile time and visible in the map file
- If a tool needs a large working buffer (e.g., logic analyzer capture), it uses a dedicated DMA-friendly buffer in a known SRAM region, **not** the state pool

### Tool Registration

```c
// tools/registry.c
static const Tool *tool_registry[] = {
    &tool_subghz_scanner,
    &tool_subghz_replay,
    &tool_subghz_analyzer,
    &tool_wifi_scan,
    &tool_wifi_monitor,
    &tool_wifi_deauth_detect,
    &tool_ble_scanner,
    &tool_ble_gatt,
    &tool_i2c_scanner,
    &tool_spi_monitor,
    &tool_uart_bridge,
    &tool_logic_analyzer,
    &tool_gpio_control,
    &tool_ir_remote,
    &tool_settings,
    &tool_about,
    NULL
};
```

### Tool Lifecycle

```
User selects "Sub-GHz Scanner" from menu
  │
  ├── Engine checks tool->requires
  │   ├── REQUIRE_RADIO1 → is CC1101 #1 responsive? Yes → continue
  │   ├── REQUIRE_ORCA → is Bottlenose present? N/A → continue
  │   └── REQUIRE_HEADER_POWER → is pin 4 powered? N/A → continue
  │
  ├── tool->init(ctx)        ← Allocate state, configure hardware
  ├── tool->render(ctx)      ← Draw initial UI
  ├── Main loop:
  │   ├── Button event → tool->on_button(ctx, evt)
  │   ├── Timer tick → tool->on_timer(ctx, tick)
  │   ├── Data arrives → tool->on_data(ctx, evt)
  │   └── After any handler → tool->render(ctx) if dirty
  │
  ├── User presses Red (back)
  │   ├── tool->stop(ctx)
  │   ├── tool->destroy(ctx)  ← Free state, reset hardware
  │   └── Return to parent menu
```

---

## 7. Tool Inventory — v1 Firmware

### Sub-GHz (requires: CC1101 radios)

| Tool | Description | Gray Action |
|------|-------------|-------------|
| **Scanner** | Sweep 300-928 MHz, show active frequencies + signal strength. Dual-radio simultaneous monitoring. Spectrum waterfall display. | Toggle band |
| **Receiver** | Tune to a frequency, decode and display incoming signals. ASK/OOK/FSK demod. Save captures as .sub files. | Start/stop capture |
| **Transmitter** | Replay saved .sub files or craft manual transmissions. | Send |
| **Frequency Analyzer** | Real-time spectrum view across selected band. Both radios for wider coverage. | Switch radio |

### WiFi (requires: Bottlenose Orca)

| Tool | Description | Gray Action |
|------|-------------|-------------|
| **AP Scanner** | List all visible access points. SSID, BSSID, channel, RSSI, auth type, band. Sortable columns. | Toggle sort |
| **Probe Monitor** | Capture and display probe requests — shows device MACs and SSIDs they're looking for. | Clear list |
| **Deauth Detector** | Monitor for deauthentication frames on selected channel(s). Alert with count + source. | Channel cycle |

### BLE (requires: Bottlenose Orca)

| Tool | Description | Gray Action |
|------|-------------|-------------|
| **Device Scanner** | List BLE advertisements: MAC, name, RSSI, type. Live-updating. | Toggle filter |
| **GATT Explorer** | Connect to a device, enumerate services/characteristics/descriptors. Read values. | Read selected |

### Bus Analysis (requires: header power on pin 4)

| Tool | Description | Gray Action |
|------|-------------|-------------|
| **I2C Scanner** | Probe all 7-bit addresses (0x03-0x77), show responding devices. Identify known ICs. | Rescan |
| **SPI Monitor** | Capture SPI traffic (CS/MOSI/MISO/SCLK) and display decoded transactions. | Start/stop |
| **UART Bridge** | Two-way UART terminal. Configurable baud/format. Hex and ASCII views. | Toggle hex/ascii |
| **Logic Analyzer** | FPGA-based capture at up to 31.25 MHz. Sigrok-compatible output. Trigger on edge/pattern. | Trigger mode |

### GPIO

| Tool | Description | Gray Action |
|------|-------------|-------------|
| **Pin Control** | Set individual pins high/low/PWM/input. See state of all pins at a glance. | Toggle selected pin |

### Utility

| Tool | Description | Gray Action |
|------|-------------|-------------|
| **IR Remote** | Transmit and receive IR codes (NEC, RC5, etc.). Save/load profiles. | Send last code |
| **Settings** | Display brightness, LED behavior, audio volume, radio defaults, button hold time, filesystem info. | — |
| **About** | Firmware versions (Main, Display, Bottlenose), hardware info, battery stats, uptime. | — |

---

## 8. Hardware Abstraction Layer (HAL)

### RadioHAL (CC1101)

```c
typedef struct {
    // Configuration
    void (*set_frequency)(uint32_t freq_hz);
    void (*set_modulation)(Modulation mod);      // OOK, ASK, 2FSK, 4FSK, GFSK, MSK
    void (*set_bandwidth)(uint32_t bw_hz);
    void (*set_data_rate)(uint32_t baud);
    void (*set_tx_power)(int8_t dbm);

    // Operation
    void (*rx_start)(void);
    void (*rx_stop)(void);
    void (*tx_data)(const uint8_t *data, size_t len);
    void (*tx_sub_file)(const char *path);       // Transmit .sub file
    int  (*get_rssi)(void);                      // Current RSSI in dBm

    // Raw register access
    uint8_t (*read_reg)(uint8_t addr);
    void (*write_reg)(uint8_t addr, uint8_t val);

    // Callbacks
    void (*on_rx_data)(RadioRxCallback cb);      // Register data callback
    void (*on_rx_carrier)(RadioCarrierCallback cb); // RSSI threshold callback
} RadioHAL;
```

### OrcaHAL (Bottlenose)

```c
typedef struct {
    bool present;                                // Detected at boot?
    char version[32];                            // Firmware version string

    // WiFi
    void (*wifi_scan)(WiFiBand band);            // Start AP scan
    void (*wifi_scan_stop)(void);
    void (*wifi_monitor)(uint8_t channel);       // Enter monitor mode
    void (*wifi_deauth_detect)(uint8_t *channels, uint8_t count);
    void (*wifi_probe_listen)(void);

    // BLE
    void (*ble_scan)(BLEScanParams *params);
    void (*ble_scan_stop)(void);
    void (*ble_connect)(uint8_t *mac, uint8_t addr_type);
    void (*ble_gatt_discover)(void);
    void (*ble_disconnect)(void);

    // Callbacks
    void (*on_wifi_ap)(WiFiAPCallback cb);
    void (*on_wifi_frame)(WiFiFrameCallback cb);
    void (*on_wifi_deauth)(WiFiDeauthCallback cb);
    void (*on_wifi_probe)(WiFiProbeCallback cb);
    void (*on_ble_adv)(BLEAdvCallback cb);
    void (*on_ble_gatt)(BLEGattCallback cb);

    // Control
    void (*ping)(void);
    void (*reset)(void);
} OrcaHAL;
```

### FPGAHAL

```c
typedef struct {
    void (*load_bitstream)(const uint8_t *data, size_t len);
    void (*load_from_file)(const char *name);
    void (*reset)(void);
    bool (*is_configured)(void);                 // Read CDONE

    // Logic analyzer
    void (*la_start)(uint32_t sample_rate, uint8_t pin_mask, TriggerDef *trigger);
    void (*la_stop)(void);
    void (*la_read)(uint8_t *buffer, size_t *len);

    // High-speed IO
    void (*hs_enable)(bool en);
} FPGAHAL;
```

### BusHAL

```c
typedef struct {
    // I2C
    int  (*i2c_scan)(uint8_t *found_addrs, size_t max);
    int  (*i2c_read)(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);
    int  (*i2c_write)(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);

    // SPI
    int  (*spi_transfer)(const uint8_t *tx, uint8_t *rx, size_t len);
    void (*spi_set_freq)(uint32_t hz);
    void (*spi_set_mode)(uint8_t cpol, uint8_t cpha);

    // UART (external)
    void (*uart_set_baud)(uint32_t baud);
    void (*uart_set_format)(uint8_t data_bits, uint8_t parity, uint8_t stop_bits);
    int  (*uart_write)(const uint8_t *data, size_t len);
    int  (*uart_read)(uint8_t *data, size_t max, uint32_t timeout_ms);
    void (*uart_on_rx)(UartRxCallback cb);
} BusHAL;
```

---

## 9. Storage Layout

```
Main RP2040 Flash (16MB)
/
├── firmware.bin              # Active firmware image
├── settings.json             # User settings (JSON, human-editable)
├── captures/
│   ├── subghz/               # .sub files (Flipper-compatible)
│   ├── wifi/                 # .pcap or .csv exports
│   ├── ble/                  # .csv or .json device logs
│   ├── bus/                  # .csv or binary bus captures
│   └── logic/                # .sr (Sigrok session) or .vcd
├── ir/
│   └── remotes/              # Saved IR remote profiles (.json)
├── radio/
│   └── presets/              # Named radio configs (frequency, mod, rate)
└── fpga/
    └── bitstreams/           # Custom FPGA configs (.bin)

Display RP2040 Flash (16MB)
/
├── firmware.bin
├── settings.json
├── fonts/                    # Bitmap fonts for display rendering
├── icons/                    # Menu and status bar icons (.fwi)
└── splash/                   # Boot splash images
```

### Settings Schema (settings.json)

```json
{
    "display": {
        "brightness": 200,
        "auto_dim_sec": 30,
        "orientation": 0
    },
    "audio": {
        "volume": 50,
        "system_sounds": true,
        "key_click": true
    },
    "radio": {
        "default_freq_1": 433920000,
        "default_freq_2": 315000000,
        "default_mod": "OOK",
        "default_bandwidth": 58035
    },
    "power": {
        "auto_off_min": 10,
        "sleep_after_sec": 60
    },
    "ui": {
        "button_hold_ms": 500,
        "scroll_speed": 3,
        "menu_wrap": true
    }
}
```

---

## 10. Host Serial Interface (USB CDC)

When connected to a PC, the Main RP2040's USB CDC provides a CLI:

```
$ help
Akhlut CFW v1.0.0

commands:
  info                    Device info, firmware versions, battery
  tools                   List available tools
  tool <slug> [args]      Launch a tool (e.g., tool subghz_scan --freq 433920000)
  radio <1|2> <cmd>       Direct radio control
  gpio <pin> <cmd>        Direct GPIO control
  i2c <cmd>               I2C operations
  spi <cmd>               SPI operations
  uart <cmd>              UART bridge
  orca <cmd>              Bottlenose commands (if present)
  fpga <cmd>              FPGA operations
  fs <cmd>                Filesystem operations (ls, cat, rm, put, get)
  settings [key] [value]  View or change settings
  reboot                  Reboot device
  bootloader              Enter UF2 bootloader mode
```

### Scriptable Mode

For automation, the CLI accepts `--json` flag to output structured JSON:
```
$ tool subghz_scan --freq 433920000 --json
{"type":"carrier","freq":433920000,"rssi":-45,"timestamp":1234567890}
{"type":"carrier","freq":433920000,"rssi":-48,"timestamp":1234567891}
```

This enables integration with Python scripts, logging pipelines, or custom GUIs without needing the `freewili` Python package.

---

## 11. Bottlenose Firmware

The ESP32-C5 needs its own firmware — the stock Bottlenose firmware (if any) is a websocket relay, not a security tool.

### Architecture

```c
// Main loop — command/response slave
while (1) {
    IPPFrame frame = ipp_receive(&uart);

    switch (frame.type) {
        case WIFI_SCAN_START:
            wifi_scan_start(frame.payload);
            break;
        case BLE_SCAN_START:
            ble_scan_start(frame.payload);
            break;
        // ...
    }
}

// Async callbacks push results back to Main
void wifi_scan_result_cb(wifi_ap_record_t *ap) {
    IPPFrame response = {
        .type = WIFI_AP_FOUND,
        .payload = serialize_ap(ap)
    };
    ipp_send(&uart, &response);
}
```

### Build System
- ESP-IDF (Espressif's official SDK for ESP32-C5)
- Custom partition table (larger app partition, no OTA for now)
- Flash via UART from Main RP2040 or directly via USB (Maestro Orca debug board)

---

## 12. Build System

### Repository Structure

```
akhlut/
├── main/                    # Main RP2040 firmware
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c           # Entry point, boot, main loop
│   │   ├── ipp.c/h          # Inter-processor protocol
│   │   ├── tool_engine.c/h  # Tool lifecycle manager
│   │   ├── hal/             # Hardware abstraction
│   │   │   ├── radio.c/h    # CC1101 driver
│   │   │   ├── fpga.c/h     # iCE40 control
│   │   │   ├── bus.c/h      # SPI1/I2C0/UART1 external
│   │   │   ├── gpio.c/h     # External GPIO
│   │   │   └── orca.c/h     # Bottlenose communication
│   │   ├── tools/           # One .c/.h per tool
│   │   │   ├── subghz_scanner.c
│   │   │   ├── wifi_scan.c
│   │   │   ├── i2c_scanner.c
│   │   │   └── ...
│   │   ├── cli.c/h          # Host serial CLI
│   │   └── settings.c/h     # Settings load/save
│   ├── include/
│   │   └── board.h          # FreeWili 1 pin definitions
│   └── freewili1.uf2        # Build output
│
├── display/                 # Display RP2040 firmware
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.c           # Entry point, boot, event loop
│   │   ├── ipp.c/h          # Protocol (Display side)
│   │   ├── ui/              # UI rendering engine
│   │   │   ├── renderer.c/h # Core drawing primitives
│   │   │   ├── menu.c/h     # Menu widget
│   │   │   ├── table.c/h    # Data table widget
│   │   │   ├── graph.c/h    # Graph/spectrum widget
│   │   │   ├── text.c/h     # Text screen widget
│   │   │   ├── dialog.c/h   # Dialog widget
│   │   │   └── status.c/h   # Status bar + hint bar
│   │   ├── hal/
│   │   │   ├── tft.c/h      # LCD driver (SPI1)
│   │   │   ├── buttons.c/h  # Button scanner + debounce
│   │   │   ├── leds.c/h     # WS2812 driver (GP7)
│   │   │   ├── audio.c/h    # I2S audio (GP4-6)
│   │   │   ├── ir.c/h       # IR TX (GP9) / RX (GP16)
│   │   │   └── sensors.c/h  # LIS3DH, BQ25892, MCP7940, PCA9555
│   │   └── assets/          # Fonts, icons, splash images
│   └── freewili1_display.uf2
│
├── bottlenose/              # ESP32-C5 firmware
│   ├── CMakeLists.txt       # ESP-IDF project
│   ├── main/
│   │   ├── main.c           # Entry, UART init, command loop
│   │   ├── ipp.c/h          # Protocol (Bottlenose side)
│   │   ├── wifi.c/h         # WiFi scan, monitor, probe capture
│   │   └── ble.c/h          # BLE scan, connect, GATT
│   └── bottlenose.bin
│
├── common/                  # Shared across all three
│   ├── ipp_defs.h           # Protocol message types, structs
│   └── formats.h            # Capture file format helpers
│
├── tools/                   # Host-side Python tools
│   ├── flash.py             # Flash all three processors
│   ├── cli.py               # Interactive host CLI
│   └── capture_viewer.py    # View/convert capture files
│
├── docs/                    # Our documentation
│   └── hardware-bible.md
│
└── README.md
```

### Toolchain Requirements

| Target | Toolchain | Build |
|--------|-----------|-------|
| Main RP2040 | Pico SDK + arm-none-eabi-gcc | CMake → .uf2 |
| Display RP2040 | Pico SDK + arm-none-eabi-gcc | CMake → .uf2 |
| Bottlenose ESP32-C5 | ESP-IDF + riscv32 toolchain | idf.py build → .bin |

### Flash Procedure

```bash
# Flash Display first (Main controls Display's RUN pin)
python tools/flash.py display display/freewili1_display.uf2

# Flash Main
python tools/flash.py main main/freewili1.uf2

# Flash Bottlenose (via Main's UART passthrough or direct USB)
python tools/flash.py bottlenose bottlenose/bottlenose.bin
```

---

## 13. Project Name

The firmware needs a name. Suggestions for Chris to pick:

**Akhlut CFW** — Custom firmware for the FreeWili 1 OG.

Or Chris names it. The name goes in the splash screen, CLI banner, and repo.

---

## 14. Implementation Order

| Phase | What | Delivers |
|-------|------|----------|
| **2a** | `board.h` + IPP + boot skeleton | Both RP2040s boot, handshake, show splash, display a static menu |
| **2b** | UI framework on Display | Menu navigation, all widgets, status bar, hint bar — button-driven |
| **2c** | CC1101 driver + Sub-GHz Scanner | First real tool working end-to-end |
| **2d** | Bus tools (I2C/SPI/UART) | External protocol analysis working |
| **2e** | Bottlenose firmware + WiFi/BLE | Wireless tools online |
| **2f** | FPGA logic analyzer integration | Sigrok-compatible capture from device |
| **2g** | Host CLI + capture export | PC integration, scriptable automation |
| **2h** | Polish: settings, power management, IR | Complete platform |

---

*This is a living spec. Every section will be refined as implementation reveals constraints.*
