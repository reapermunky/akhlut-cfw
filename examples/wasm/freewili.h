/**
 * freewili.h — FreeWili WASM App SDK v2
 *
 * Include this in your C app. Compile with:
 *   clang --target=wasm32 -nostdlib -O2 -Wl,--no-entry \
 *         -Wl,--export=_start -Wl,--initial-memory=131072 \
 *         -o myapp.wasm myapp.c
 *
 * All functions are imported from the "env" module.
 * No float/double support — use integer arithmetic only.
 *
 * Functions marked [T1] are Tier 1 (implementable now).
 * Functions marked [T2] need Display firmware additions.
 * Functions marked [T3] need major new subsystems.
 * Unmarked functions are the original 19 (all working).
 */

#ifndef FREEWILI_H
#define FREEWILI_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;
typedef signed short       int16_t;

/* ══════════════════════════════════════════════════════════
 * Constants
 * ══════════════════════════════════════════════════════════ */

/* RGB565 color helpers */
#define RGB565(r, g, b) \
    ((uint16_t)(((r) & 0x1F) << 11 | ((g) & 0x3F) << 5 | ((b) & 0x1F)))

#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_ORANGE  0xFD20
#define COLOR_PURPLE  0x881F
#define COLOR_GRAY    0x8410
#define COLOR_DARK    0x2104

/* Button IDs (matches IPP defines) */
#define BTN_GRAY    0
#define BTN_YELLOW  1
#define BTN_GREEN   2
#define BTN_BLUE    3
#define BTN_RED     4
#define BTN_NONE    0xFF

/* IR protocol IDs */
#define IR_NEC      0
#define IR_SONY     1
#define IR_RC5      2
#define IR_RC6      3
#define IR_SAMSUNG  4

/* Radio modulation modes */
#define RADIO_MOD_2FSK  0
#define RADIO_MOD_GFSK  1
#define RADIO_MOD_ASK   3
#define RADIO_MOD_4FSK  4
#define RADIO_MOD_MSK   7

/* Charging state flags (from app_get_charging) */
#define CHARGE_CHARGING  (1 << 0)
#define CHARGE_COMPLETE  (1 << 1)
#define CHARGE_USB_IN    (1 << 2)

/* Screen dimensions */
#define SCREEN_W  320
#define SCREEN_H  240

/* Accelerometer data offsets (10-byte buffer from app_get_accel) */
#define ACCEL_X_OFFSET       0   /* int16_t, milli-g */
#define ACCEL_Y_OFFSET       2   /* int16_t, milli-g */
#define ACCEL_Z_OFFSET       4   /* int16_t, milli-g */
#define ACCEL_GTOTAL_OFFSET  6   /* uint16_t, milli-g */
#define ACCEL_TEMP_OFFSET    8   /* int16_t, temp_C * 10 */
#define ACCEL_BUF_SIZE       10

/* IR receive buffer offsets (6-byte buffer from app_ir_recv) */
#define IR_PROTO_OFFSET  0   /* uint8_t */
#define IR_CODE_OFFSET   1   /* uint32_t LE */
#define IR_BITS_OFFSET   5   /* uint8_t */
#define IR_BUF_SIZE      6

/* RTC time buffer offsets (7-byte buffer from app_get_time) */
#define RTC_SEC_OFFSET   0   /* 0-59 */
#define RTC_MIN_OFFSET   1   /* 0-59 */
#define RTC_HR_OFFSET    2   /* 0-23 */
#define RTC_DOW_OFFSET   3   /* 1-7 */
#define RTC_DAY_OFFSET   4   /* 1-31 */
#define RTC_MON_OFFSET   5   /* 1-12 */
#define RTC_YR_OFFSET    6   /* 0-99 (2000+) */
#define RTC_BUF_SIZE     7

/* ══════════════════════════════════════════════════════════
 * Display — Original (6 functions)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_clear_screen")))
void app_clear_screen(void);

__attribute__((import_module("env"), import_name("app_draw_text")))
void app_draw_text(uint32_t x, uint32_t y,
                   const void *text, uint32_t len, uint32_t color);

__attribute__((import_module("env"), import_name("app_draw_rect")))
void app_draw_rect(uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h,
                   uint32_t color, uint32_t filled);

__attribute__((import_module("env"), import_name("app_draw_line")))
void app_draw_line(uint32_t x0, uint32_t y0,
                   uint32_t x1, uint32_t y1, uint32_t color);

__attribute__((import_module("env"), import_name("app_fb_flip")))
void app_fb_flip(void);

__attribute__((import_module("env"), import_name("app_toast")))
void app_toast(const void *text, uint32_t len, uint32_t duration_ms);

/* ══════════════════════════════════════════════════════════
 * Display — New (4 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T1] Draw text using the small 5x8 font */
__attribute__((import_module("env"), import_name("app_draw_text_small")))
void app_draw_text_small(uint32_t x, uint32_t y,
                         const void *text, uint32_t len, uint32_t color);

/* [T1] Draw a single pixel (convenience; uses 1x1 rect internally) */
__attribute__((import_module("env"), import_name("app_set_pixel")))
void app_set_pixel(uint32_t x, uint32_t y, uint32_t color);

/* [T2] Draw a circle (filled or outline) */
__attribute__((import_module("env"), import_name("app_draw_circle")))
void app_draw_circle(uint32_t cx, uint32_t cy, uint32_t r,
                     uint32_t color, uint32_t filled);

/* [T1] Set display backlight brightness (0=off, 255=max) */
__attribute__((import_module("env"), import_name("app_set_backlight")))
void app_set_backlight(uint32_t brightness);

/* [T2] Show a progress bar overlay (percent 0-100) */
__attribute__((import_module("env"), import_name("app_progress")))
void app_progress(uint32_t percent, const void *text, uint32_t len);

/* ══════════════════════════════════════════════════════════
 * Input — Original (2 functions)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_get_button")))
uint32_t app_get_button(void);

__attribute__((import_module("env"), import_name("app_wait_button")))
uint32_t app_wait_button(void);

/* ══════════════════════════════════════════════════════════
 * Radio — Original (3 functions, Radio 1 only)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_radio_set_freq")))
void app_radio_set_freq(uint32_t freq_hz);

__attribute__((import_module("env"), import_name("app_radio_get_rssi")))
int32_t app_radio_get_rssi(void);

__attribute__((import_module("env"), import_name("app_radio_tx")))
void app_radio_tx(const void *data, uint32_t len);

/* ══════════════════════════════════════════════════════════
 * Radio — New (5 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T1] Receive a radio packet. Returns bytes received, 0 if none. */
__attribute__((import_module("env"), import_name("app_radio_rx")))
int32_t app_radio_rx(void *buf, uint32_t max_len);

/* [T1] Configure Radio 1 modulation, data rate (baud), RX bandwidth (Hz) */
__attribute__((import_module("env"), import_name("app_radio_set_config")))
void app_radio_set_config(uint32_t modulation, uint32_t data_rate, uint32_t rx_bw);

/* [T1] Tune Radio 2 to a frequency (Hz) */
__attribute__((import_module("env"), import_name("app_radio2_set_freq")))
void app_radio2_set_freq(uint32_t freq_hz);

/* [T1] Read RSSI from Radio 2 (dBm) */
__attribute__((import_module("env"), import_name("app_radio2_get_rssi")))
int32_t app_radio2_get_rssi(void);

/* [T1] Transmit via Radio 2 */
__attribute__((import_module("env"), import_name("app_radio2_tx")))
void app_radio2_tx(const void *data, uint32_t len);

/* ══════════════════════════════════════════════════════════
 * GPIO — Original (2 functions)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_gpio_read")))
uint32_t app_gpio_read(uint32_t pin);

__attribute__((import_module("env"), import_name("app_gpio_write")))
void app_gpio_write(uint32_t pin, uint32_t value);

/* ══════════════════════════════════════════════════════════
 * Storage — Original (2 functions)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_fs_write")))
int32_t app_fs_write(const void *path, uint32_t path_len,
                     const void *data, uint32_t data_len);

__attribute__((import_module("env"), import_name("app_fs_read")))
int32_t app_fs_read(const void *path, uint32_t path_len,
                    void *buf, uint32_t buf_max);

/* ══════════════════════════════════════════════════════════
 * Storage — New (2 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T1] Delete a file (must be under /apps/) */
__attribute__((import_module("env"), import_name("app_fs_delete")))
int32_t app_fs_delete(const void *path, uint32_t path_len);

/* [T1] List files in /apps/. Writes null-separated names. Returns file count. */
__attribute__((import_module("env"), import_name("app_fs_list")))
int32_t app_fs_list(void *buf, uint32_t max_len);

/* ══════════════════════════════════════════════════════════
 * LEDs — Original (1 function)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_set_led")))
void app_set_led(uint32_t index, uint32_t r, uint32_t g, uint32_t b);

/* ══════════════════════════════════════════════════════════
 * Sensors — New (5 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T2] Read accelerometer. Writes 10 bytes to buf (see ACCEL_* offsets).
 * Returns 0 on success, -1 if accelerometer not present. */
__attribute__((import_module("env"), import_name("app_get_accel")))
int32_t app_get_accel(void *buf);

/* [T2] Read RTC time. Writes 7 bytes to buf (see RTC_* offsets).
 * Returns 0 on success, -1 if RTC not present. */
__attribute__((import_module("env"), import_name("app_get_time")))
int32_t app_get_time(void *buf);

/* [T1] Get battery voltage in millivolts (e.g. 3850 = 3.85V) */
__attribute__((import_module("env"), import_name("app_get_battery_mv")))
uint32_t app_get_battery_mv(void);

/* [T1] Get charging state flags (CHARGE_CHARGING | CHARGE_COMPLETE | CHARGE_USB_IN) */
__attribute__((import_module("env"), import_name("app_get_charging")))
uint32_t app_get_charging(void);

/* [T2] Read/write PCA9555 I/O expander register.
 * Write: app_ioexp_write(reg, value) returns 0=ok.
 * Read:  app_ioexp_read(reg) returns register value or -1. */
__attribute__((import_module("env"), import_name("app_ioexp_write")))
int32_t app_ioexp_write(uint32_t reg, uint32_t value);

__attribute__((import_module("env"), import_name("app_ioexp_read")))
int32_t app_ioexp_read(uint32_t reg);

/* ══════════════════════════════════════════════════════════
 * IR — New (2 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T1] Transmit an IR code (protocol: IR_NEC..IR_SAMSUNG) */
__attribute__((import_module("env"), import_name("app_ir_send")))
void app_ir_send(uint32_t protocol, uint32_t code, uint32_t bits);

/* [T1] Poll for received IR code. Writes 6 bytes to buf (see IR_* offsets).
 * Returns 1 if code available, 0 if none. */
__attribute__((import_module("env"), import_name("app_ir_recv")))
int32_t app_ir_recv(void *buf);

/* ══════════════════════════════════════════════════════════
 * External Buses — New (3 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T1] I2C transfer on external bus (header pins 8/10).
 * addr: 7-bit I2C address (0x08-0x77).
 * Write tx_len bytes from tx, then read rx_len bytes into rx.
 * Set tx_len=0 for read-only, rx_len=0 for write-only.
 * Returns 0 on success, -1 on error. */
__attribute__((import_module("env"), import_name("app_ext_i2c_xfer")))
int32_t app_ext_i2c_xfer(uint32_t addr, const void *tx, uint32_t tx_len,
                          void *rx, uint32_t rx_len);

/* [T1] SPI transfer on external bus (header pins 1/12/13/15).
 * Full-duplex: sends tx while receiving into rx. len bytes both ways.
 * Returns 0 on success, -1 on error. */
__attribute__((import_module("env"), import_name("app_ext_spi_xfer")))
int32_t app_ext_spi_xfer(const void *tx, void *rx, uint32_t len);

/* [T3] Write to external UART (header pins 5/9).
 * Returns bytes written, -1 if UART busy (Orca connected). */
__attribute__((import_module("env"), import_name("app_ext_uart_write")))
int32_t app_ext_uart_write(const void *data, uint32_t len);

/* [T3] Read from external UART.
 * Returns bytes read (0 if none available), -1 if UART busy. */
__attribute__((import_module("env"), import_name("app_ext_uart_read")))
int32_t app_ext_uart_read(void *buf, uint32_t max_len);

/* ══════════════════════════════════════════════════════════
 * WiFi/BLE (via Bottlenose/Orca) — New (4 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T3] Start WiFi AP scan. Results arrive asynchronously. */
__attribute__((import_module("env"), import_name("app_wifi_scan_start")))
void app_wifi_scan_start(void);

/* [T3] Get WiFi scan results. Writes packed AP records into buf.
 * Returns number of APs found, 0 if scan in progress, -1 if no Orca. */
__attribute__((import_module("env"), import_name("app_wifi_scan_get")))
int32_t app_wifi_scan_get(void *buf, uint32_t max_len);

/* [T3] Start BLE advertisement scan. */
__attribute__((import_module("env"), import_name("app_ble_scan_start")))
void app_ble_scan_start(void);

/* [T3] Get BLE scan results. Returns number of devices, 0 if in progress, -1 if no Orca. */
__attribute__((import_module("env"), import_name("app_ble_scan_get")))
int32_t app_ble_scan_get(void *buf, uint32_t max_len);

/* ══════════════════════════════════════════════════════════
 * Audio — New (2 functions)
 * ══════════════════════════════════════════════════════════ */

/* [T3] Play a tone through the I2S speaker */
__attribute__((import_module("env"), import_name("app_audio_tone")))
void app_audio_tone(uint32_t freq_hz, uint32_t duration_ms);

/* [T3] Read PCM samples from PDM microphone.
 * Returns samples read, -1 if mic not available. */
__attribute__((import_module("env"), import_name("app_mic_read")))
int32_t app_mic_read(void *buf, uint32_t samples);

/* ══════════════════════════════════════════════════════════
 * System — Original (3 functions)
 * ══════════════════════════════════════════════════════════ */

__attribute__((import_module("env"), import_name("app_sleep_ms")))
void app_sleep_ms(uint32_t ms);

__attribute__((import_module("env"), import_name("app_exit")))
void app_exit(void);

__attribute__((import_module("env"), import_name("app_get_battery_pct")))
uint32_t app_get_battery_pct(void);

/* ══════════════════════════════════════════════════════════
 * System — New (1 function)
 * ══════════════════════════════════════════════════════════ */

/* [T1] Get millisecond tick count (wraps at ~49 days) */
__attribute__((import_module("env"), import_name("app_get_ticks")))
uint32_t app_get_ticks(void);

/* ══════════════════════════════════════════════════════════
 * Helpers (inline, no host function needed)
 * ══════════════════════════════════════════════════════════ */

static inline uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline void draw_str(uint32_t x, uint32_t y,
                             const char *s, uint32_t color) {
    app_draw_text(x, y, s, str_len(s), color);
}

static inline void draw_str_small(uint32_t x, uint32_t y,
                                   const char *s, uint32_t color) {
    app_draw_text_small(x, y, s, str_len(s), color);
}

static inline void *memcpy_inline(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static inline int16_t read_i16(const void *buf, uint32_t offset) {
    const uint8_t *p = (const uint8_t *)buf + offset;
    return (int16_t)(p[0] | (p[1] << 8));
}

static inline uint16_t read_u16(const void *buf, uint32_t offset) {
    const uint8_t *p = (const uint8_t *)buf + offset;
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t read_u32(const void *buf, uint32_t offset) {
    const uint8_t *p = (const uint8_t *)buf + offset;
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/* Integer square root (for g_total computation) */
static inline uint32_t isqrt(uint32_t n) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > n) bit >>= 2;
    while (bit) {
        if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* Integer absolute value */
static inline int32_t iabs(int32_t x) {
    return x < 0 ? -x : x;
}

/* Simple integer to string */
static inline uint32_t itoa_buf(int32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return 1; }
    uint32_t neg = 0, i = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[12];
    while (val > 0 && i < 11) { tmp[i++] = '0' + (val % 10); val /= 10; }
    uint32_t p = 0;
    if (neg) buf[p++] = '-';
    while (i > 0) buf[p++] = tmp[--i];
    buf[p] = 0;
    return p;
}

#endif
