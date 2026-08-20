# Changelog

## v1.1.0 — 2026-08-20

### WASM API Expansion
- Added 35 new host functions (47 total): draw_text_small, set_pixel, draw_circle, set_backlight, progress bar, wait_button, dual radio TX/RX, radio config, GPIO read/write, filesystem (write/read/delete/list), LED control, accelerometer, time, battery voltage/charge status, I/O expander, IR send/recv, I2C/SPI/UART buses, WiFi/BLE scan stubs, audio stubs
- Updated `freewili.h` SDK header with all 47 function declarations and helper utilities (isqrt, iabs, itoa_buf, read_i16/u16/u32, memcpy_inline)
- Added accelerometer offset defines (X, Y, Z, GTOTAL, TEMP) and buffer size constant

### WASM API Bug Fixes (Code Review)
- Fixed `app_get_accel` memory safety: added bounds check before writing to WASM linear memory
- Fixed `app_fs_read` return value: now returns bytes read instead of error code on success
- Fixed `app_ir_recv` bounds check: validates output buffer fits within WASM memory
- Fixed `app_ext_i2c_xfer` bounds check: validates both TX and RX buffers
- Fixed `app_ext_spi_xfer` bounds check: validates buffer size before SPI transfer
- Fixed `app_ext_uart_read` bounds check: validates output buffer within WASM memory
- Fixed `app_wifi_scan_get` / `app_ble_scan_get` bounds checks: validate output buffers
- Added WASM force-stop checks (`WASM_CHECK_STOP`) to `app_wait_button` and `app_sleep_ms` to prevent hung apps

### New WASM App: G-Force Meter (`gforce.wasm`)
- Measures dynamic acceleration with gravity subtracted (stationary reads ~0g in any orientation)
- Bar gauge display with color-coded intensity (green/cyan/yellow/orange/red)
- Peak hold tracking with GREEN button reset
- 60-sample scrolling history graph
- Uses pre-computed `g_total` from LIS3DH accelerometer (sqrt of x^2+y^2+z^2 minus 1g)
- Modeled on rssi_meter's proven rendering pattern for stability

### WASM Runtime Fix
- Identified critical crash-on-relaunch bug: WASM apps must exit with bare `app_exit()/return` — sending display commands (clear_screen, fb_flip) before app_exit corrupts runtime state, causing hard crash on next app launch

### Firmware
- Rebuilt both main and display firmware with all WASM API fixes
- Distributed firmware and WASM apps to all locations (build/, firmware/, installer/)

## v1.0.1 — 2026-08-18

### GhostESP / WiFi
- Fixed AP selection: re-scans before attack to ensure GhostESP has fresh results
- Fixed raw mode toggle between scan, select, and deauth — raw mode stays on from scan through AP table
- Added 5GHz AP guard: channels 36+ show "5G" label, attack commands blocked with toast
- Added Orca cleanup on tool exit — all running commands stopped when leaving GhostESP

### Installer
- UI overhaul: side-by-side layout with scrollable log panel
- Three-button flow: Install from Stock, Update Akhlut, Restore Stock FW
- Fixed flash ordering (Display first, then Main)
- Added restore timing warning (5-10 min for stock firmware)

### Stability
- Fixed requirements.txt (added missing `freewili` package)
- Various serial timing and buffer handling fixes

## v1.0.0 — 2026-08-15

Initial release.
