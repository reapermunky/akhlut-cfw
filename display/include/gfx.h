/**
 * gfx.h — Graphics Library for FreeWili 1 Display
 *
 * Direct-to-TFT rendering with region-based updates.
 * No full framebuffer (would consume 153KB of 264KB SRAM).
 * Instead: set a draw region, push pixels.
 *
 * Design: dark background, crisp monospace text, high contrast.
 * Dense, clean layout optimized for readability.
 */

#ifndef GFX_H
#define GFX_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "board_display.h"

/* ──────────────────────────────────────────────────────────
 * RGB565 Color Helpers
 * ────────────────────────────────────────────────────────── */
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/* ──────────────────────────────────────────────────────────
 * Palette — Dark, high-contrast, deliberate
 *
 * Clean, dense, readable in bright light and dark rooms.
 * ────────────────────────────────────────────────────────── */
#define COL_BG          0x0000      // Pure black — maximum contrast
#define COL_TEXT        0xDEFB      // Cool off-white (#DEE2E6)
#define COL_DIM         0x8C51      // Medium gray — secondary text (#8C9196)
#define COL_HIGHLIGHT   0x2C9F      // Bright cyan (#2CB5FF) — selected items
#define COL_HL_BG       0x0926      // Dark cyan tint (#092830) — selection background
#define COL_ACTIVE      0x07E0      // Pure green (#00FF00) — active/running
#define COL_WARN        0xFD20      // Amber (#FFA500) — warnings
#define COL_ERROR       0xF800      // Pure red (#FF0000) — errors
#define COL_DISABLED    0x4A49      // Dark gray (#4A4A4A) — unavailable items
#define COL_DIVIDER     0x2104      // Subtle gray (#202020) — lines, borders
#define COL_STATUS_BG   0x0841      // Near-black (#080808) — status bar bg
#define COL_ACCENT      0xB7FF      // Ice blue (#B4DFFF) — icons, labels

/* ──────────────────────────────────────────────────────────
 * Font System — Embedded Bitmap Fonts
 *
 * Two sizes:
 *   FONT_SMALL (5x8)  — Status bar, hint bar, secondary text
 *   FONT_MAIN  (6x10) — Menu items, primary content
 * ────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t         width;
    uint8_t         height;
    uint8_t         first_char;
    uint8_t         last_char;
    const uint8_t  *data;       // Packed bitmap, 1 byte per row per char
} gfx_font_t;

extern const gfx_font_t font_small;   // 5x8
extern const gfx_font_t font_main;    // 6x10

#define FONT_SMALL  (&font_small)
#define FONT_MAIN   (&font_main)

/* ──────────────────────────────────────────────────────────
 * Init / Low-Level TFT
 * ────────────────────────────────────────────────────────── */
void gfx_init(void);

/* ──────────────────────────────────────────────────────────
 * Region-Based Drawing
 *
 * All drawing ops work on the TFT directly via SPI.
 * Set a window, push pixels. No framebuffer needed.
 * ────────────────────────────────────────────────────────── */

// Fill a rectangle with a solid color
void gfx_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// Draw a 1px rectangle outline
void gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// Horizontal line (fast — single SPI burst)
void gfx_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color);

// Vertical line
void gfx_vline(uint16_t x, uint16_t y, uint16_t h, uint16_t color);

// Single pixel
void gfx_pixel(uint16_t x, uint16_t y, uint16_t color);

// Fill entire screen
void gfx_clear(uint16_t color);

/* ──────────────────────────────────────────────────────────
 * Text Rendering
 * ────────────────────────────────────────────────────────── */

// Draw a single character. Returns the x advance.
uint8_t gfx_draw_char(uint16_t x, uint16_t y, char c,
                       const gfx_font_t *font, uint16_t fg, uint16_t bg);

// Draw a null-terminated string. Returns total width drawn.
uint16_t gfx_draw_str(uint16_t x, uint16_t y, const char *str,
                       const gfx_font_t *font, uint16_t fg, uint16_t bg);

// Draw string with max width (truncates with "..." if needed)
uint16_t gfx_draw_str_trunc(uint16_t x, uint16_t y, const char *str,
                             const gfx_font_t *font, uint16_t fg, uint16_t bg,
                             uint16_t max_width);

// Measure string width in pixels without drawing
uint16_t gfx_str_width(const char *str, const gfx_font_t *font);

// Draw right-aligned string
uint16_t gfx_draw_str_right(uint16_t right_x, uint16_t y, const char *str,
                              const gfx_font_t *font, uint16_t fg, uint16_t bg);

/* ──────────────────────────────────────────────────────────
 * Icons (8x8 1-bit bitmaps)
 * ────────────────────────────────────────────────────────── */
typedef struct {
    uint8_t data[8]; // 8 rows, 8 bits each, MSB-first
} gfx_icon_t;

void gfx_draw_icon(uint16_t x, uint16_t y, const gfx_icon_t *icon,
                    uint16_t fg, uint16_t bg);

/* ──────────────────────────────────────────────────────────
 * Progress Bar
 * ────────────────────────────────────────────────────────── */
void gfx_draw_progress(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        uint8_t percent, uint16_t fg, uint16_t bg);

#endif // GFX_H
