# FreeWili WASM App SDK

Write apps in C, compile to WebAssembly, run on the FreeWili. No standard library needed — the firmware provides everything through imported host functions.

## Quick Start

1. Include `freewili.h` (from `examples/wasm/`)
2. Write a `_start()` function as your entry point
3. Compile with clang targeting wasm32
4. Upload the `.wasm` file to the device

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

### Build

```bash
clang --target=wasm32 -nostdlib -O2 \
      -Wl,--no-entry -Wl,--export=_start \
      -Wl,--initial-memory=65536 -Wl,-z,stack-size=8192 \
      -o myapp.wasm myapp.c
```

Or use the Makefile in `examples/wasm/`:

```bash
cd examples/wasm
make
```

### Install

Upload via the installer GUI (WASM Apps > Add .wasm), or copy to `/apps/` on the device filesystem using the serial upload command.

### Constraints

- **Max file size:** 32 KB
- **Max apps:** 16 files in `/apps/`
- **No floats/doubles** — use integer arithmetic only
- **No stdlib** — `freewili.h` provides its own type definitions
- **Entry point:** `_start()` (exported, no arguments, no return)

---

## Display API

### `app_clear_screen()`

Clear the entire screen to black.

```c
void app_clear_screen(void);
```

Call this at the start of each frame before drawing.

---

### `app_draw_text(x, y, text, len, color)`

Draw a text string at pixel coordinates.

```c
void app_draw_text(uint32_t x, uint32_t y,
                   const void *text, uint32_t len,
                   uint32_t color);
```

| Parameter | Description |
|-----------|-------------|
| `x`, `y` | Top-left pixel position (0,0 = top-left of screen) |
| `text` | Pointer to string data (not null-terminated) |
| `len` | Number of bytes to draw |
| `color` | RGB565 color value |

Uses the 6x10 main font. Screen is 320x240 — fits 53 characters per line, 24 lines.

**Convenience wrapper** (from `freewili.h`):
```c
draw_str(80, 10, "Hello!", COLOR_WHITE);
```

---

### `app_draw_rect(x, y, w, h, color, filled)`

Draw a rectangle (filled or outline).

```c
void app_draw_rect(uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h,
                   uint32_t color, uint32_t filled);
```

| Parameter | Description |
|-----------|-------------|
| `x`, `y` | Top-left corner |
| `w`, `h` | Width and height in pixels |
| `color` | RGB565 color |
| `filled` | `1` = solid fill, `0` = 1px outline |

```c
// Filled green bar
app_draw_rect(10, 50, 200, 20, COLOR_GREEN, 1);

// Red outline box
app_draw_rect(10, 80, 200, 20, COLOR_RED, 0);
```

---

### `app_draw_line(x0, y0, x1, y1, color)`

Draw a line between two points.

```c
void app_draw_line(uint32_t x0, uint32_t y0,
                   uint32_t x1, uint32_t y1,
                   uint32_t color);
```

```c
app_draw_line(0, 120, 319, 120, COLOR_WHITE);  // horizontal divider
```

---

### `app_fb_flip()`

Flush the current frame to the display.

```c
void app_fb_flip(void);
```

Call once per frame after all draw calls. Without this, nothing appears on screen.

**Typical frame loop:**
```c
while (1) {
    app_clear_screen();
    // ... draw calls ...
    app_fb_flip();
    app_sleep_ms(100);
}
```

---

### `app_toast(text, len, duration_ms)`

Show a temporary overlay message on screen.

```c
void app_toast(const void *text, uint32_t len, uint32_t duration_ms);
```

| Parameter | Description |
|-----------|-------------|
| `text` | Message text (max 63 bytes) |
| `len` | Length of message |
| `duration_ms` | How long to show (milliseconds) |

```c
const char *msg = "Saved!";
app_toast(msg, 6, 1500);
```

---

## Input API

### `app_get_button()`

Poll for a button press (non-blocking).

```c
uint32_t app_get_button(void);
```

Returns the button ID if pressed, or `BTN_NONE` (0xFF) if no button is down.

| Return | Button |
|--------|--------|
| `BTN_GRAY` (0) | Gray — context action |
| `BTN_YELLOW` (1) | Yellow — up |
| `BTN_GREEN` (2) | Green — select/confirm |
| `BTN_BLUE` (3) | Blue — down |
| `BTN_RED` (4) | Red — back/cancel |
| `BTN_NONE` (0xFF) | No button pressed |

```c
uint32_t btn = app_get_button();
if (btn == BTN_RED) {
    app_exit();
}
```

---

### `app_wait_button()`

Block until a button is pressed.

```c
uint32_t app_wait_button(void);
```

Returns the button ID. Blocks execution — use `app_get_button()` for non-blocking input in animation loops.

The firmware checks for force-stop (user holding RED in the tool engine) during the wait, so the app can always be killed externally.

```c
draw_str(80, 110, "Press any button...", COLOR_WHITE);
app_fb_flip();
uint32_t btn = app_wait_button();
```

---

## Radio API

### `app_radio_set_freq(freq_hz)`

Tune Radio 1 (CC1101) to a frequency.

```c
void app_radio_set_freq(uint32_t freq_hz);
```

| Parameter | Description |
|-----------|-------------|
| `freq_hz` | Frequency in Hz (e.g., `433920000` for 433.920 MHz) |

Puts the radio into receive mode at the specified frequency. Supported range: 300-928 MHz (ISM bands).

```c
app_radio_set_freq(433920000);  // 433.920 MHz
```

---

### `app_radio_get_rssi()`

Read the current received signal strength.

```c
int32_t app_radio_get_rssi(void);
```

Returns RSSI in dBm (typically -120 to 0). Call after `app_radio_set_freq()`.

```c
int32_t rssi = app_radio_get_rssi();
// rssi is negative: -70 dBm = moderate signal
```

---

### `app_radio_tx(data, len)`

Transmit raw data via Radio 1.

```c
void app_radio_tx(const void *data, uint32_t len);
```

| Parameter | Description |
|-----------|-------------|
| `data` | Pointer to transmit data |
| `len` | Number of bytes to send (max 64) |

Set the frequency with `app_radio_set_freq()` first.

```c
uint8_t payload[] = {0xAA, 0x55, 0xDE, 0xAD};
app_radio_set_freq(433920000);
app_radio_tx(payload, 4);
```

---

## GPIO API

### `app_gpio_read(pin)`

Read the state of an external GPIO pin.

```c
uint32_t app_gpio_read(uint32_t pin);
```

Returns `1` (high) or `0` (low). Only external header pins are accessible:

| Pin | Header Position |
|-----|----------------|
| 24 | Pin 14 (input) |
| 25 | Pin 17 (output) |
| 26 | Pin 14 (alt) |

Returns `0` for disallowed pins.

```c
uint32_t state = app_gpio_read(24);
```

---

### `app_gpio_write(pin, value)`

Set an external GPIO pin high or low.

```c
void app_gpio_write(uint32_t pin, uint32_t value);
```

| Parameter | Description |
|-----------|-------------|
| `pin` | GPIO pin number (24, 25, or 26 only) |
| `value` | `0` = low, non-zero = high |

Automatically configures the pin as output on first write.

```c
app_gpio_write(25, 1);    // set high
app_sleep_ms(500);
app_gpio_write(25, 0);    // set low
```

---

## Storage API

File storage is restricted to the `/apps/` directory on the flash filesystem.

### `app_fs_write(path, path_len, data, data_len)`

Write data to a file.

```c
int32_t app_fs_write(const void *path, uint32_t path_len,
                     const void *data, uint32_t data_len);
```

| Parameter | Description |
|-----------|-------------|
| `path` | File path string (must start with `/apps/`) |
| `path_len` | Length of path string |
| `data` | Data to write |
| `data_len` | Number of bytes |

Returns `0` on success, negative on error.

```c
const char *path = "/apps/mydata.bin";
uint8_t data[] = {1, 2, 3, 4};
int32_t err = app_fs_write(path, 16, data, 4);
```

---

### `app_fs_read(path, path_len, buf, buf_max)`

Read data from a file.

```c
int32_t app_fs_read(const void *path, uint32_t path_len,
                    void *buf, uint32_t buf_max);
```

| Parameter | Description |
|-----------|-------------|
| `path` | File path string (must start with `/apps/`) |
| `path_len` | Length of path string |
| `buf` | Destination buffer in WASM memory |
| `buf_max` | Maximum bytes to read |

Returns number of bytes read on success, negative on error.

```c
uint8_t buf[64];
const char *path = "/apps/mydata.bin";
int32_t n = app_fs_read(path, 16, buf, 64);
```

---

## LED API

### `app_set_led(index, r, g, b)`

Set a WS2812 LED color.

```c
void app_set_led(uint32_t index, uint32_t r, uint32_t g, uint32_t b);
```

| Parameter | Description |
|-----------|-------------|
| `index` | LED index (0-6, 7 LEDs on the board) |
| `r`, `g`, `b` | Color components (0-255 each) |

```c
app_set_led(0, 255, 0, 0);    // LED 0 red
app_set_led(1, 0, 255, 0);    // LED 1 green
app_set_led(2, 0, 0, 255);    // LED 2 blue
```

---

## System API

### `app_sleep_ms(ms)`

Sleep for a specified duration.

```c
void app_sleep_ms(uint32_t ms);
```

| Parameter | Description |
|-----------|-------------|
| `ms` | Milliseconds to sleep (max 10,000) |

The firmware checks for force-stop during sleep, so long sleeps won't prevent the user from killing the app.

```c
app_sleep_ms(100);  // 100ms delay
```

---

### `app_exit()`

Terminate the app and return to the app browser.

```c
void app_exit(void);
```

Call this when the user presses RED to exit, or when the app is done.

```c
if (app_get_button() == BTN_RED) {
    app_exit();
}
```

---

### `app_get_battery_pct()`

Get the current battery percentage.

```c
uint32_t app_get_battery_pct(void);
```

Returns battery level 0-100. Currently returns 0 (placeholder for future implementation).

---

## Color Reference (RGB565)

The display uses RGB565 color encoding. Pre-defined constants:

| Constant | Value | Color |
|----------|-------|-------|
| `COLOR_BLACK` | `0x0000` | Black |
| `COLOR_WHITE` | `0xFFFF` | White |
| `COLOR_RED` | `0xF800` | Red |
| `COLOR_GREEN` | `0x07E0` | Green |
| `COLOR_BLUE` | `0x001F` | Blue |
| `COLOR_YELLOW` | `0xFFE0` | Yellow |
| `COLOR_CYAN` | `0x07FF` | Cyan |

Custom colors:

```c
// RGB565(r5, g6, b5) — 5-bit red, 6-bit green, 5-bit blue
uint16_t orange = RGB565(31, 32, 0);
uint16_t purple = RGB565(16, 0, 31);
uint16_t dark_gray = 0x4208;
```

## Complete Example

See `examples/wasm/rssi_meter.c` for a full working app that:
- Tunes the CC1101 radio to 433.920 MHz
- Reads RSSI continuously
- Draws a live bar graph and history chart
- Adjusts frequency with YELLOW/BLUE buttons
- Exits cleanly with RED
