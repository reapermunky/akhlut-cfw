/**
 * gfx.c — Graphics Library Implementation
 *
 * Direct-to-TFT rendering. All SPI operations are blocking
 * on Core 0. DMA will be added in Phase 2 for Core 1 flush.
 */

#include "gfx.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

/* ──────────────────────────────────────────────────────────
 * Embedded Font: 5x8 (ASCII 32-126)
 * Clean, readable at small sizes. Status bar, hints.
 * Each char is 5 bytes (5 columns, 8 rows, column-major).
 * ────────────────────────────────────────────────────────── */
static const uint8_t font_5x8_data[] = {
    // Space (32)
    0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (33)
    0x00, 0x00, 0x5F, 0x00, 0x00,
    // " (34)
    0x00, 0x07, 0x00, 0x07, 0x00,
    // # (35)
    0x14, 0x7F, 0x14, 0x7F, 0x14,
    // $ (36)
    0x24, 0x2A, 0x7F, 0x2A, 0x12,
    // % (37)
    0x23, 0x13, 0x08, 0x64, 0x62,
    // & (38)
    0x36, 0x49, 0x55, 0x22, 0x50,
    // ' (39)
    0x00, 0x05, 0x03, 0x00, 0x00,
    // ( (40)
    0x00, 0x1C, 0x22, 0x41, 0x00,
    // ) (41)
    0x00, 0x41, 0x22, 0x1C, 0x00,
    // * (42)
    0x08, 0x2A, 0x1C, 0x2A, 0x08,
    // + (43)
    0x08, 0x08, 0x3E, 0x08, 0x08,
    // , (44)
    0x00, 0x50, 0x30, 0x00, 0x00,
    // - (45)
    0x08, 0x08, 0x08, 0x08, 0x08,
    // . (46)
    0x00, 0x60, 0x60, 0x00, 0x00,
    // / (47)
    0x20, 0x10, 0x08, 0x04, 0x02,
    // 0-9 (48-57)
    0x3E, 0x51, 0x49, 0x45, 0x3E,
    0x00, 0x42, 0x7F, 0x40, 0x00,
    0x42, 0x61, 0x51, 0x49, 0x46,
    0x21, 0x41, 0x45, 0x4B, 0x31,
    0x18, 0x14, 0x12, 0x7F, 0x10,
    0x27, 0x45, 0x45, 0x45, 0x39,
    0x3C, 0x4A, 0x49, 0x49, 0x30,
    0x01, 0x71, 0x09, 0x05, 0x03,
    0x36, 0x49, 0x49, 0x49, 0x36,
    0x06, 0x49, 0x49, 0x29, 0x1E,
    // : (58)
    0x00, 0x36, 0x36, 0x00, 0x00,
    // ; (59)
    0x00, 0x56, 0x36, 0x00, 0x00,
    // < (60)
    0x00, 0x08, 0x14, 0x22, 0x41,
    // = (61)
    0x14, 0x14, 0x14, 0x14, 0x14,
    // > (62)
    0x41, 0x22, 0x14, 0x08, 0x00,
    // ? (63)
    0x02, 0x01, 0x51, 0x09, 0x06,
    // @ (64)
    0x32, 0x49, 0x79, 0x41, 0x3E,
    // A-Z (65-90)
    0x7E, 0x11, 0x11, 0x11, 0x7E,
    0x7F, 0x49, 0x49, 0x49, 0x36,
    0x3E, 0x41, 0x41, 0x41, 0x22,
    0x7F, 0x41, 0x41, 0x22, 0x1C,
    0x7F, 0x49, 0x49, 0x49, 0x41,
    0x7F, 0x09, 0x09, 0x01, 0x01,
    0x3E, 0x41, 0x41, 0x51, 0x32,
    0x7F, 0x08, 0x08, 0x08, 0x7F,
    0x00, 0x41, 0x7F, 0x41, 0x00,
    0x20, 0x40, 0x41, 0x3F, 0x01,
    0x7F, 0x08, 0x14, 0x22, 0x41,
    0x7F, 0x40, 0x40, 0x40, 0x40,
    0x7F, 0x02, 0x04, 0x02, 0x7F,
    0x7F, 0x04, 0x08, 0x10, 0x7F,
    0x3E, 0x41, 0x41, 0x41, 0x3E,
    0x7F, 0x09, 0x09, 0x09, 0x06,
    0x3E, 0x41, 0x51, 0x21, 0x5E,
    0x7F, 0x09, 0x19, 0x29, 0x46,
    0x46, 0x49, 0x49, 0x49, 0x31,
    0x01, 0x01, 0x7F, 0x01, 0x01,
    0x3F, 0x40, 0x40, 0x40, 0x3F,
    0x1F, 0x20, 0x40, 0x20, 0x1F,
    0x7F, 0x20, 0x18, 0x20, 0x7F,
    0x63, 0x14, 0x08, 0x14, 0x63,
    0x03, 0x04, 0x78, 0x04, 0x03,
    0x61, 0x51, 0x49, 0x45, 0x43,
    // [ (91)
    0x00, 0x00, 0x7F, 0x41, 0x41,
    // backslash (92)
    0x02, 0x04, 0x08, 0x10, 0x20,
    // ] (93)
    0x41, 0x41, 0x7F, 0x00, 0x00,
    // ^ (94)
    0x04, 0x02, 0x01, 0x02, 0x04,
    // _ (95)
    0x40, 0x40, 0x40, 0x40, 0x40,
    // ` (96)
    0x00, 0x01, 0x02, 0x04, 0x00,
    // a-z (97-122)
    0x20, 0x54, 0x54, 0x54, 0x78,
    0x7F, 0x48, 0x44, 0x44, 0x38,
    0x38, 0x44, 0x44, 0x44, 0x20,
    0x38, 0x44, 0x44, 0x48, 0x7F,
    0x38, 0x54, 0x54, 0x54, 0x18,
    0x08, 0x7E, 0x09, 0x01, 0x02,
    0x08, 0x14, 0x54, 0x54, 0x3C,
    0x7F, 0x08, 0x04, 0x04, 0x78,
    0x00, 0x44, 0x7D, 0x40, 0x00,
    0x20, 0x40, 0x44, 0x3D, 0x00,
    0x00, 0x7F, 0x10, 0x28, 0x44,
    0x00, 0x41, 0x7F, 0x40, 0x00,
    0x7C, 0x04, 0x18, 0x04, 0x78,
    0x7C, 0x08, 0x04, 0x04, 0x78,
    0x38, 0x44, 0x44, 0x44, 0x38,
    0x7C, 0x14, 0x14, 0x14, 0x08,
    0x08, 0x14, 0x14, 0x18, 0x7C,
    0x7C, 0x08, 0x04, 0x04, 0x08,
    0x48, 0x54, 0x54, 0x54, 0x20,
    0x04, 0x3F, 0x44, 0x40, 0x20,
    0x3C, 0x40, 0x40, 0x20, 0x7C,
    0x1C, 0x20, 0x40, 0x20, 0x1C,
    0x3C, 0x40, 0x30, 0x40, 0x3C,
    0x44, 0x28, 0x10, 0x28, 0x44,
    0x0C, 0x50, 0x50, 0x50, 0x3C,
    0x44, 0x64, 0x54, 0x4C, 0x44,
    // { (123)
    0x00, 0x08, 0x36, 0x41, 0x00,
    // | (124)
    0x00, 0x00, 0x7F, 0x00, 0x00,
    // } (125)
    0x00, 0x41, 0x36, 0x08, 0x00,
    // ~ (126)
    0x08, 0x08, 0x2A, 0x1C, 0x08,
};

const gfx_font_t font_small = {
    .width = 5,
    .height = 8,
    .first_char = 32,
    .last_char = 126,
    .data = font_5x8_data,
};

/* ──────────────────────────────────────────────────────────
 * Embedded Font: 6x10 (ASCII 32-126)
 *
 * Main UI font. Built from the 5x8 font with 1px padding
 * on right and 2px padding on bottom for better readability.
 * Same glyph data, different metrics.
 * ────────────────────────────────────────────────────────── */
const gfx_font_t font_main = {
    .width = 6,       // 5px glyph + 1px spacing
    .height = 10,     // 8px glyph + 2px line spacing
    .first_char = 32,
    .last_char = 126,
    .data = font_5x8_data,  // Same glyphs, renderer handles padding
};

/* ──────────────────────────────────────────────────────────
 * TFT SPI Helpers (inline for speed)
 * ────────────────────────────────────────────────────────── */
static inline void tft_cmd(uint8_t cmd) {
    gpio_put(PIN_TFT_DC, 0);
    gpio_put(PIN_TFT_CS, 0);
    spi_write_blocking(TFT_SPI, &cmd, 1);
    gpio_put(PIN_TFT_CS, 1);
}

static inline void tft_data_bytes(const uint8_t *data, size_t len) {
    gpio_put(PIN_TFT_DC, 1);
    gpio_put(PIN_TFT_CS, 0);
    spi_write_blocking(TFT_SPI, data, len);
    gpio_put(PIN_TFT_CS, 1);
}

static inline void tft_data16(uint16_t val) {
    uint8_t buf[2] = { val >> 8, val & 0xFF };
    gpio_put(PIN_TFT_DC, 1);
    gpio_put(PIN_TFT_CS, 0);
    spi_write_blocking(TFT_SPI, buf, 2);
    gpio_put(PIN_TFT_CS, 1);
}

/* ──────────────────────────────────────────────────────────
 * Set draw window on TFT
 * ────────────────────────────────────────────────────────── */
static void tft_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x1 = x + w - 1;
    uint16_t y1 = y + h - 1;

    tft_cmd(0x2A);  // Column address set
    uint8_t col[] = { x >> 8, x & 0xFF, x1 >> 8, x1 & 0xFF };
    tft_data_bytes(col, 4);

    tft_cmd(0x2B);  // Row address set
    uint8_t row[] = { y >> 8, y & 0xFF, y1 >> 8, y1 & 0xFF };
    tft_data_bytes(row, 4);

    tft_cmd(0x2C);  // Memory write
}

/* ──────────────────────────────────────────────────────────
 * Push a run of identical pixels (fast fill)
 * ────────────────────────────────────────────────────────── */
static void tft_push_color(uint16_t color, uint32_t count) {
    // Use a small buffer for burst writes
    uint8_t buf[64];
    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    for (int i = 0; i < 32; i++) {
        buf[i * 2] = hi;
        buf[i * 2 + 1] = lo;
    }

    gpio_put(PIN_TFT_DC, 1);
    gpio_put(PIN_TFT_CS, 0);

    while (count >= 32) {
        spi_write_blocking(TFT_SPI, buf, 64);
        count -= 32;
    }
    if (count > 0) {
        spi_write_blocking(TFT_SPI, buf, count * 2);
    }

    gpio_put(PIN_TFT_CS, 1);
}

/* ──────────────────────────────────────────────────────────
 * Push a row of mixed pixels (for text rendering)
 * ────────────────────────────────────────────────────────── */
static void tft_push_pixels(const uint16_t *pixels, uint32_t count) {
    // Swap bytes for SPI (MSB first)
    uint8_t buf[128];
    gpio_put(PIN_TFT_DC, 1);
    gpio_put(PIN_TFT_CS, 0);

    while (count > 0) {
        uint32_t batch = count > 64 ? 64 : count;
        for (uint32_t i = 0; i < batch; i++) {
            buf[i * 2] = pixels[i] >> 8;
            buf[i * 2 + 1] = pixels[i] & 0xFF;
        }
        spi_write_blocking(TFT_SPI, buf, batch * 2);
        pixels += batch;
        count -= batch;
    }

    gpio_put(PIN_TFT_CS, 1);
}

/* ──────────────────────────────────────────────────────────
 * Public API: Init
 * ────────────────────────────────────────────────────────── */
void gfx_init(void) {
    // SPI1 at max practical speed
    spi_init(TFT_SPI, 62500000);
    gpio_set_function(PIN_TFT_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_TFT_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_TFT_CS);
    gpio_set_dir(PIN_TFT_CS, GPIO_OUT);
    gpio_put(PIN_TFT_CS, 1);

    gpio_init(PIN_TFT_DC);
    gpio_set_dir(PIN_TFT_DC, GPIO_OUT);

    // Backlight PWM
    gpio_set_function(PIN_TFT_BACKLIGHT, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_TFT_BACKLIGHT);
    pwm_set_wrap(slice, 255);
    pwm_set_chan_level(slice, PWM_CHAN_B, 200);
    pwm_set_enabled(slice, true);

    // TFT init sequence (ILI9341/ST7789 compatible)
    tft_cmd(0x01); // SWRESET
    sleep_ms(150);
    tft_cmd(0x11); // SLPOUT
    sleep_ms(50);

    tft_cmd(0x3A); // COLMOD
    uint8_t pixfmt = 0x55; // 16bpp RGB565
    tft_data_bytes(&pixfmt, 1);

    tft_cmd(0x36); // MADCTL — landscape, column flip, BGR color order
    uint8_t madctl = 0x68;
    tft_data_bytes(&madctl, 1);

    tft_cmd(0x21); // INVON — invert display polarity for this panel

    tft_cmd(0x29); // DISPON
    sleep_ms(50);

    gfx_clear(COL_BG);
}

/* ──────────────────────────────────────────────────────────
 * Public API: Drawing Primitives
 * ────────────────────────────────────────────────────────── */
void gfx_clear(uint16_t color) {
    tft_set_window(0, 0, TFT_WIDTH, TFT_HEIGHT);
    tft_push_color(color, (uint32_t)TFT_WIDTH * TFT_HEIGHT);
}

void gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;

    tft_set_window(x, y, w, h);
    tft_push_color(color, (uint32_t)w * h);
}

void gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color) {
    gfx_fill_rect(x, y, w, 1, color);
}

void gfx_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color) {
    gfx_fill_rect(x, y, 1, h, color);
}

void gfx_pixel(uint16_t x, uint16_t y, uint16_t color) {
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT) return;
    tft_set_window(x, y, 1, 1);
    tft_data16(color);
}

/* ──────────────────────────────────────────────────────────
 * Public API: Text Rendering
 * ────────────────────────────────────────────────────────── */
uint8_t gfx_draw_char(uint16_t x, uint16_t y, char c,
                       const gfx_font_t *font, uint16_t fg, uint16_t bg) {
    if (c < font->first_char || c > font->last_char) c = '?';

    uint16_t idx = (c - font->first_char) * 5; // Always 5 bytes per glyph
    const uint8_t *glyph = &font->data[idx];

    // Render into a pixel buffer (font->width × font->height)
    uint16_t pixels[10 * 8]; // Max 6×10 = 60 pixels, plenty
    uint16_t glyph_w = font->width;
    uint16_t glyph_h = font->height;

    for (uint16_t row = 0; row < glyph_h; row++) {
        for (uint16_t col = 0; col < glyph_w; col++) {
            bool lit = false;
            if (col < 5 && row < 8) {
                // Column-major: glyph[col] has the bits for that column
                lit = (glyph[col] >> row) & 1;
            }
            pixels[row * glyph_w + col] = lit ? fg : bg;
        }
    }

    tft_set_window(x, y, glyph_w, glyph_h);
    tft_push_pixels(pixels, glyph_w * glyph_h);

    return glyph_w;
}

uint16_t gfx_draw_str(uint16_t x, uint16_t y, const char *str,
                       const gfx_font_t *font, uint16_t fg, uint16_t bg) {
    uint16_t start_x = x;
    while (*str) {
        if (x + font->width > TFT_WIDTH) break;
        x += gfx_draw_char(x, y, *str, font, fg, bg);
        str++;
    }
    return x - start_x;
}

uint16_t gfx_draw_str_trunc(uint16_t x, uint16_t y, const char *str,
                             const gfx_font_t *font, uint16_t fg, uint16_t bg,
                             uint16_t max_width) {
    uint16_t start_x = x;
    uint16_t ellipsis_w = font->width * 3; // "..."

    while (*str) {
        uint16_t remaining = max_width - (x - start_x);

        // Check if we need to truncate
        if (remaining <= ellipsis_w && *(str + 1)) {
            gfx_draw_char(x, y, '.', font, fg, bg);
            x += font->width;
            gfx_draw_char(x, y, '.', font, fg, bg);
            x += font->width;
            gfx_draw_char(x, y, '.', font, fg, bg);
            x += font->width;
            break;
        }

        if (x + font->width > start_x + max_width) break;
        x += gfx_draw_char(x, y, *str, font, fg, bg);
        str++;
    }
    return x - start_x;
}

uint16_t gfx_str_width(const char *str, const gfx_font_t *font) {
    uint16_t w = 0;
    while (*str) {
        w += font->width;
        str++;
    }
    return w;
}

uint16_t gfx_draw_str_right(uint16_t right_x, uint16_t y, const char *str,
                              const gfx_font_t *font, uint16_t fg, uint16_t bg) {
    uint16_t w = gfx_str_width(str, font);
    uint16_t x = (right_x > w) ? right_x - w : 0;
    return gfx_draw_str(x, y, str, font, fg, bg);
}

/* ──────────────────────────────────────────────────────────
 * Public API: Icons
 * ────────────────────────────────────────────────────────── */
void gfx_draw_icon(uint16_t x, uint16_t y, const gfx_icon_t *icon,
                    uint16_t fg, uint16_t bg) {
    uint16_t pixels[64]; // 8x8

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            bool lit = (icon->data[row] >> (7 - col)) & 1;
            pixels[row * 8 + col] = lit ? fg : bg;
        }
    }

    tft_set_window(x, y, 8, 8);
    tft_push_pixels(pixels, 64);
}

/* ──────────────────────────────────────────────────────────
 * Public API: Progress Bar
 * ────────────────────────────────────────────────────────── */
void gfx_draw_progress(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint8_t percent, uint16_t fg, uint16_t bg) {
    if (percent > 100) percent = 100;

    // Background
    gfx_fill_rect(x, y, w, h, bg);

    // Fill
    uint16_t fill_w = (w * percent) / 100;
    if (fill_w > 0) {
        gfx_fill_rect(x, y, fill_w, h, fg);
    }

    // Border
    gfx_draw_rect(x, y, w, h, COL_DIM);
}
