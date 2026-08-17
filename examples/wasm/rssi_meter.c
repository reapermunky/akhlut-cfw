/**
 * rssi_meter.c — Signal Strength Meter
 *
 * FreeWili WASM App Example
 *
 * Tunes Radio 1 to 433.920 MHz and displays live RSSI
 * as a bar graph. YELLOW/BLUE adjust frequency.
 * RED exits.
 *
 * Build:
 *   clang --target=wasm32 -nostdlib -O2 \
 *         -Wl,--no-entry -Wl,--export=_start \
 *         -Wl,--initial-memory=131072 \
 *         -o rssi_meter.wasm rssi_meter.c
 */

#include "freewili.h"

#define SCREEN_W    320
#define SCREEN_H    240
#define BAR_X       40
#define BAR_Y       80
#define BAR_W       240
#define BAR_H       40
#define HISTORY_Y   140
#define HISTORY_H   80
#define HISTORY_N   60

static int32_t  history[HISTORY_N];
static uint32_t hist_idx = 0;
static uint32_t freq_mhz = 433;
static uint32_t freq_khz = 920;

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

static uint16_t rssi_color(int32_t rssi) {
    if (rssi > -50) return COLOR_GREEN;
    if (rssi > -70) return COLOR_YELLOW;
    return COLOR_RED;
}

static void draw_frame(int32_t rssi) {
    app_clear_screen();

    /* Title */
    draw_str(80, 10, "RSSI METER", COLOR_WHITE);

    /* Frequency display */
    char freq_buf[20];
    char mhz_str[6], khz_str[6];
    itoa_simple((int32_t)freq_mhz, mhz_str);
    itoa_simple((int32_t)freq_khz, khz_str);

    int p = 0;
    const char *s;
    for (s = mhz_str; *s; s++) freq_buf[p++] = *s;
    freq_buf[p++] = '.';
    /* zero-pad khz to 3 digits */
    if (freq_khz < 100) freq_buf[p++] = '0';
    if (freq_khz < 10)  freq_buf[p++] = '0';
    for (s = khz_str; *s; s++) freq_buf[p++] = *s;
    freq_buf[p++] = ' ';
    freq_buf[p++] = 'M';
    freq_buf[p++] = 'H';
    freq_buf[p++] = 'z';
    freq_buf[p] = 0;

    draw_str(100, 35, freq_buf, COLOR_CYAN);

    /* RSSI number */
    char rssi_str[16];
    itoa_simple(rssi, rssi_str);
    int q = 0;
    while (rssi_str[q]) q++;
    rssi_str[q++] = ' ';
    rssi_str[q++] = 'd';
    rssi_str[q++] = 'B';
    rssi_str[q++] = 'm';
    rssi_str[q] = 0;

    draw_str(BAR_X, BAR_Y - 15, rssi_str, COLOR_WHITE);

    /* RSSI bar background */
    app_draw_rect(BAR_X, BAR_Y, BAR_W, BAR_H, 0x4208, 1);

    /* RSSI bar filled portion: map -120..0 dBm to 0..BAR_W */
    int32_t clamped = rssi;
    if (clamped < -120) clamped = -120;
    if (clamped > 0) clamped = 0;
    uint32_t bar_fill = (uint32_t)((clamped + 120) * (int32_t)BAR_W / 120);
    if (bar_fill > BAR_W) bar_fill = BAR_W;

    if (bar_fill > 0)
        app_draw_rect(BAR_X, BAR_Y, bar_fill, BAR_H, rssi_color(rssi), 1);

    /* History graph */
    app_draw_rect(BAR_X, HISTORY_Y, BAR_W, HISTORY_H, 0x2104, 1);
    draw_str(BAR_X, HISTORY_Y - 12, "History", 0x8410);

    for (uint32_t i = 0; i < HISTORY_N; i++) {
        uint32_t idx = (hist_idx + i) % HISTORY_N;
        int32_t r = history[idx];
        if (r < -120) r = -120;
        if (r > 0) r = 0;
        uint32_t h = (uint32_t)((r + 120) * (int32_t)HISTORY_H / 120);
        if (h > HISTORY_H) h = HISTORY_H;
        uint32_t x = BAR_X + (i * BAR_W / HISTORY_N);
        uint32_t w = BAR_W / HISTORY_N;
        if (w < 1) w = 1;
        if (h > 0) {
            uint32_t y_bar = HISTORY_Y + HISTORY_H - h;
            app_draw_rect(x, y_bar, w, h, rssi_color(r), 1);
        }
    }

    /* Controls hint */
    draw_str(20, SCREEN_H - 18, "[Y/B] Freq  [RED] Exit", 0x8410);

    app_fb_flip();
}

void _start(void) {
    /* Initialize history */
    for (int i = 0; i < HISTORY_N; i++)
        history[i] = -120;

    /* Set initial frequency */
    uint32_t freq_hz = freq_mhz * 1000000 + freq_khz * 1000;
    app_radio_set_freq(freq_hz);

    app_toast("RSSI Meter", 10, 1500);

    while (1) {
        /* Read RSSI */
        int32_t rssi = app_radio_get_rssi();

        /* Store in history */
        history[hist_idx] = rssi;
        hist_idx = (hist_idx + 1) % HISTORY_N;

        /* Draw */
        draw_frame(rssi);

        /* Check buttons (non-blocking) */
        uint32_t btn = app_get_button();
        if (btn == BTN_RED) {
            app_exit();
            return;
        }
        if (btn == BTN_YELLOW) {
            /* Decrease frequency by 100 kHz */
            if (freq_khz >= 100) {
                freq_khz -= 100;
            } else {
                freq_mhz--;
                freq_khz = 900;
            }
            freq_hz = freq_mhz * 1000000 + freq_khz * 1000;
            app_radio_set_freq(freq_hz);
        }
        if (btn == BTN_BLUE) {
            /* Increase frequency by 100 kHz */
            freq_khz += 100;
            if (freq_khz >= 1000) {
                freq_khz = 0;
                freq_mhz++;
            }
            freq_hz = freq_mhz * 1000000 + freq_khz * 1000;
            app_radio_set_freq(freq_hz);
        }

        app_sleep_ms(100);
    }
}
