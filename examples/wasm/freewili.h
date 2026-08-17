/**
 * freewili.h — FreeWili WASM App SDK
 *
 * Include this in your C app. Compile with:
 *   clang --target=wasm32 -nostdlib -O2 -Wl,--no-entry \
 *         -Wl,--export=_start -Wl,--initial-memory=131072 \
 *         -o myapp.wasm myapp.c
 *
 * All functions are imported from the "env" module.
 * No float/double support — use integer arithmetic only.
 */

#ifndef FREEWILI_H
#define FREEWILI_H

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef signed int         int32_t;

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

/* Button IDs (matches IPP defines) */
#define BTN_GRAY    0
#define BTN_YELLOW  1
#define BTN_GREEN   2
#define BTN_BLUE    3
#define BTN_RED     4
#define BTN_NONE    0xFF

/* ── Display ── */

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

/* ── Input ── */

__attribute__((import_module("env"), import_name("app_get_button")))
uint32_t app_get_button(void);

__attribute__((import_module("env"), import_name("app_wait_button")))
uint32_t app_wait_button(void);

/* ── Radio ── */

__attribute__((import_module("env"), import_name("app_radio_set_freq")))
void app_radio_set_freq(uint32_t freq_hz);

__attribute__((import_module("env"), import_name("app_radio_get_rssi")))
int32_t app_radio_get_rssi(void);

__attribute__((import_module("env"), import_name("app_radio_tx")))
void app_radio_tx(const void *data, uint32_t len);

/* ── GPIO ── */

__attribute__((import_module("env"), import_name("app_gpio_read")))
uint32_t app_gpio_read(uint32_t pin);

__attribute__((import_module("env"), import_name("app_gpio_write")))
void app_gpio_write(uint32_t pin, uint32_t value);

/* ── Storage ── */

__attribute__((import_module("env"), import_name("app_fs_write")))
int32_t app_fs_write(const void *path, uint32_t path_len,
                     const void *data, uint32_t data_len);

__attribute__((import_module("env"), import_name("app_fs_read")))
int32_t app_fs_read(const void *path, uint32_t path_len,
                    void *buf, uint32_t buf_max);

/* ── LEDs ── */

__attribute__((import_module("env"), import_name("app_set_led")))
void app_set_led(uint32_t index, uint32_t r, uint32_t g, uint32_t b);

/* ── System ── */

__attribute__((import_module("env"), import_name("app_sleep_ms")))
void app_sleep_ms(uint32_t ms);

__attribute__((import_module("env"), import_name("app_exit")))
void app_exit(void);

__attribute__((import_module("env"), import_name("app_get_battery_pct")))
uint32_t app_get_battery_pct(void);

/* ── Helpers ── */

static inline uint32_t str_len(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline void draw_str(uint32_t x, uint32_t y,
                             const char *s, uint32_t color) {
    app_draw_text(x, y, s, str_len(s), color);
}

#endif
