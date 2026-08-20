/**
 * gforce.c — G-Force Meter
 *
 * FreeWili WASM App
 *
 * Measures dynamic acceleration (gravity subtracted) and
 * displays it as a bar gauge with peak hold and history.
 * Works in any orientation — stationary reads ~0g.
 *
 * GREEN resets peak. RED exits.
 *
 * Build:
 *   clang --target=wasm32 -nostdlib -O2 \
 *         -Wl,--no-entry -Wl,--export=_start \
 *         -Wl,--initial-memory=131072 \
 *         -o gforce.wasm gforce.c
 */

#include "freewili.h"

void *memset(void *s, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

#define SCREEN_W    320
#define SCREEN_H    240

#define BAR_X       40
#define BAR_Y       80
#define BAR_W       240
#define BAR_H       40

#define HISTORY_Y   148
#define HISTORY_H   60
#define HISTORY_N   60

#define MAX_G_MG    3000

static int32_t  history[HISTORY_N];
static uint32_t hist_idx = 0;
static int32_t  peak_mg  = 0;
static uint8_t  abuf[ACCEL_BUF_SIZE];

static void itoa_simple(int32_t val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    char tmp[12];
    int i = 0;
    while (val > 0 && i < 11) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int p = 0;
    if (neg) buf[p++] = '-';
    while (i > 0) buf[p++] = tmp[--i];
    buf[p] = 0;
}

static uint16_t g_color(int32_t mg) {
    if (mg < 200)  return COLOR_GREEN;
    if (mg < 500)  return COLOR_CYAN;
    if (mg < 1000) return COLOR_YELLOW;
    if (mg < 2000) return COLOR_ORANGE;
    return COLOR_RED;
}

static void fmt_g(int32_t mg, char *buf) {
    char whole[6], frac[6];
    int p = 0;

    itoa_simple(mg / 1000, whole);
    int32_t f = (mg % 1000) / 10;
    itoa_simple(f, frac);

    int w = 0;
    while (whole[w]) buf[p++] = whole[w++];
    buf[p++] = '.';
    if (f < 10) buf[p++] = '0';
    w = 0;
    while (frac[w]) buf[p++] = frac[w++];
    buf[p++] = ' ';
    buf[p++] = 'g';
    buf[p] = 0;
}

static void draw_frame(int32_t dynamic_mg) {
    app_clear_screen();

    /* Title */
    draw_str(80, 8, "G-FORCE METER", COLOR_WHITE);

    /* Current g-force number */
    char gbuf[16];
    fmt_g(dynamic_mg, gbuf);
    draw_str(BAR_X, BAR_Y - 18, gbuf, g_color(dynamic_mg));

    /* Bar background */
    app_draw_rect(BAR_X, BAR_Y, BAR_W, BAR_H, 0x4208, 1);

    /* Bar fill: map 0..MAX_G_MG to 0..BAR_W */
    int32_t clamped = dynamic_mg;
    if (clamped > MAX_G_MG) clamped = MAX_G_MG;
    uint32_t bar_fill = (uint32_t)(clamped * (int32_t)BAR_W / MAX_G_MG);
    if (bar_fill > (uint32_t)BAR_W) bar_fill = BAR_W;

    if (bar_fill > 0)
        app_draw_rect(BAR_X, BAR_Y, bar_fill, BAR_H,
                      g_color(dynamic_mg), 1);

    /* Peak */
    char pkbuf[20];
    pkbuf[0] = 'P'; pkbuf[1] = 'e'; pkbuf[2] = 'a';
    pkbuf[3] = 'k'; pkbuf[4] = ' '; pkbuf[5] = ' '; pkbuf[6] = 0;
    fmt_g(peak_mg, pkbuf + 6);
    draw_str(BAR_X, BAR_Y + BAR_H + 6, pkbuf, g_color(peak_mg));

    /* History graph */
    app_draw_rect(BAR_X, HISTORY_Y, BAR_W, HISTORY_H, 0x2104, 1);
    draw_str(BAR_X, HISTORY_Y - 12, "History", 0x8410);

    for (uint32_t i = 0; i < HISTORY_N; i++) {
        uint32_t idx = (hist_idx + i) % HISTORY_N;
        int32_t val = history[idx];
        if (val > MAX_G_MG) val = MAX_G_MG;
        uint32_t h = (uint32_t)(val * (int32_t)HISTORY_H / MAX_G_MG);
        if (h > (uint32_t)HISTORY_H) h = HISTORY_H;
        uint32_t x = BAR_X + (i * BAR_W / HISTORY_N);
        uint32_t w = BAR_W / HISTORY_N;
        if (w < 1) w = 1;
        if (h > 0) {
            uint32_t y_bar = HISTORY_Y + HISTORY_H - h;
            app_draw_rect(x, y_bar, w, h, g_color(val), 1);
        }
    }

    /* Controls */
    draw_str(20, SCREEN_H - 16, "[GRN] Reset  [RED] Exit", 0x8410);

    app_fb_flip();
}

void _start(void) {
    for (uint32_t i = 0; i < HISTORY_N; i++)
        history[i] = 0;

    app_toast("G-Force Meter", 13, 1500);

    while (1) {
        int32_t dynamic_mg = 0;

        if (app_get_accel(abuf) == 0) {
            uint32_t g_total = (uint32_t)read_u16(abuf, ACCEL_GTOTAL_OFFSET);
            int32_t raw = (int32_t)g_total - 1000;
            dynamic_mg = raw < 0 ? -raw : raw;
        }

        if (dynamic_mg > peak_mg) peak_mg = dynamic_mg;

        history[hist_idx] = dynamic_mg;
        hist_idx = (hist_idx + 1) % HISTORY_N;

        draw_frame(dynamic_mg);

        uint32_t btn = app_get_button();
        if (btn == BTN_RED) {
            app_exit();
            return;
        }
        if (btn == BTN_GREEN) {
            peak_mg = 0;
            app_toast("Peak reset", 10, 500);
        }

        app_sleep_ms(100);
    }
}
