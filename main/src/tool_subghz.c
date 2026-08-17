/**
 * tool_subghz.c — Sub-GHz Scanner Tool
 *
 * Akhlut CFW
 *
 * Scans sub-GHz ISM bands using the CC1101 radios.
 * Steps through frequencies, measures RSSI at each,
 * detects active transmissions above threshold.
 *
 * Bands: 315 MHz, 433 MHz, 868 MHz, 915 MHz
 * Each step: set freq → RX → 1.5ms settle → read RSSI → IDLE
 */

#include "tool.h"
#include "board.h"
#include "cc1101.h"
#include "fs.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include <stdio.h>
#include <string.h>

#define SUBGHZ_MODE_BAND_SELECT  0
#define SUBGHZ_MODE_SCANNING     1
#define SUBGHZ_MODE_RESULTS      2
#define SUBGHZ_MODE_CAPTURING    3
#define SUBGHZ_MODE_CAPTURED     4
#define SUBGHZ_MODE_SAVED_LIST   5

#define RSSI_THRESHOLD    -75
#define MAX_SIGNALS       16
#define DISPLAY_INTERVAL  8
#define SCAN_DURATION_MS  10000

#define CAPTURE_SAMPLE_US    100
#define CAPTURE_MAX_BYTES    1400
#define CAPTURE_SILENCE_US   100000
#define CAPTURE_TIMEOUT_MS   30000
#define MIN_CAPTURE_BYTES    10

typedef struct {
    uint32_t freq;
    int8_t   rssi;
} detected_signal_t;

typedef struct {
    uint8_t  mode;
    uint8_t  band;
    uint8_t  radio_cs;
    uint32_t freq_start;
    uint32_t freq_end;
    uint32_t freq_step;
    uint16_t total_steps;
    uint16_t current_step;
    int8_t   rssi[256];
    int8_t   peak_rssi;
    uint32_t peak_freq;
    uint8_t  menu_sel;
    uint8_t  result_scroll;
    detected_signal_t signals[MAX_SIGNALS];
    uint8_t  signal_count;
    uint8_t  display_counter;
    uint32_t scan_start_ms;
    uint16_t pass;
    uint8_t  result_sel;
    uint8_t  selected_signal;
    uint16_t capture_samples;
    uint32_t capture_freq;
    uint32_t capture_start_ms;
    uint8_t  saved_sel;
    uint8_t  saved_scroll;
    uint8_t  from_saved;
    uint8_t  capture_data[CAPTURE_MAX_BYTES];
} subghz_state_t;

_Static_assert(sizeof(subghz_state_t) <= TOOL_STATE_POOL_SIZE,
               "subghz_state_t exceeds state pool");

/* ──────────────────────────────────────────────────────────
 * Band Definitions
 * ────────────────────────────────────────────────────────── */
typedef struct {
    const char *name;
    uint32_t start_hz;
    uint32_t end_hz;
    uint32_t step_hz;
} scan_band_t;

static const scan_band_t bands[] = {
    { "315 MHz",  310000000, 320000000, 200000 },
    { "433 MHz",  425000000, 445000000, 200000 },
    { "868 MHz",  862000000, 870000000, 100000 },
    { "915 MHz",  902000000, 928000000, 200000 },
};

#define BAND_COUNT (sizeof(bands) / sizeof(bands[0]))

#define MAX_SAVED_FILES  24

typedef struct __attribute__((packed)) {
    uint32_t freq;
    uint16_t samples;
    uint16_t sample_rate_us;
} subghz_file_header_t;

static char     saved_names[MAX_SAVED_FILES][32];
static uint32_t saved_sizes[MAX_SAVED_FILES];
static uint8_t  saved_count;
static uint8_t  load_buf[sizeof(subghz_file_header_t) + CAPTURE_MAX_BYTES];

/* ──────────────────────────────────────────────────────────
 * CC1101 Scanner Configuration
 *
 * Wide bandwidth (325 kHz) for signal detection.
 * OOK modulation, no sync word — just measuring RSSI.
 * Auto calibration on IDLE→RX transitions.
 * ────────────────────────────────────────────────────────── */
static const uint8_t scanner_config[][2] = {
    { CC1101_IOCFG2,   0x29 },
    { CC1101_IOCFG0,   0x2E },
    { CC1101_PKTCTRL0, 0x32 },
    { CC1101_FSCTRL1,  0x08 },
    { CC1101_MDMCFG4,  0x1C },
    { CC1101_MDMCFG3,  0x22 },
    { CC1101_MDMCFG2,  0x30 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_AGCCTRL2, 0x03 },
    { CC1101_AGCCTRL1, 0x40 },
    { CC1101_AGCCTRL0, 0x91 },
};

#define SCANNER_CONFIG_COUNT \
    (sizeof(scanner_config) / sizeof(scanner_config[0]))

static const uint8_t capture_config[][2] = {
    { CC1101_IOCFG2,   0x29 },
    { CC1101_IOCFG0,   0x0D },
    { CC1101_PKTCTRL0, 0x32 },
    { CC1101_FSCTRL1,  0x08 },
    { CC1101_MDMCFG4,  0x1C },
    { CC1101_MDMCFG3,  0x22 },
    { CC1101_MDMCFG2,  0x30 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_AGCCTRL2, 0x03 },
    { CC1101_AGCCTRL1, 0x40 },
    { CC1101_AGCCTRL0, 0x91 },
};
#define CAPTURE_CONFIG_COUNT \
    (sizeof(capture_config) / sizeof(capture_config[0]))

static const uint8_t tx_config[][2] = {
    { CC1101_IOCFG2,   0x29 },
    { CC1101_IOCFG0,   0x2E },
    { CC1101_PKTCTRL0, 0x02 },
    { CC1101_FSCTRL1,  0x08 },
    { CC1101_MDMCFG4,  0x18 },
    { CC1101_MDMCFG3,  0x93 },
    { CC1101_MDMCFG2,  0x30 },
    { CC1101_MDMCFG1,  0x00 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_FREND0,   0x11 },
};
#define TX_CONFIG_COUNT \
    (sizeof(tx_config) / sizeof(tx_config[0]))

/* Forward declarations */
static void show_band_menu(subghz_state_t *s);
static void start_scan(tool_ctx_t *ctx);
static void scan_step(subghz_state_t *s);
static void finish_scan(subghz_state_t *s);
static void show_scan_progress(subghz_state_t *s);
static void show_results(subghz_state_t *s);
static void show_capturing(subghz_state_t *s);
static void do_capture(subghz_state_t *s);
static void show_captured(subghz_state_t *s);
static void do_replay(subghz_state_t *s);
static void save_capture(subghz_state_t *s);
static void load_saved_list(subghz_state_t *s);
static void show_saved_list(subghz_state_t *s);
static void load_saved_capture(subghz_state_t *s, uint8_t idx);

static uint8_t get_gdo0_pin(uint8_t cs) {
    return (cs == PIN_RADIO1_CS) ? PIN_RADIO1_GDO0 : PIN_RADIO2_GDO0;
}

/* ──────────────────────────────────────────────────────────
 * Band Selection Menu
 * ────────────────────────────────────────────────────────── */
static void show_band_menu(subghz_state_t *s) {
    const char *items[BAND_COUNT + 1];
    uint8_t icons[BAND_COUNT + 1];
    for (int i = 0; i < (int)BAND_COUNT; i++) {
        items[i] = bands[i].name;
        icons[i] = 1;
    }
    items[BAND_COUNT] = "Saved Signals";
    icons[BAND_COUNT] = 11;
    tool_send_menu(s->menu_sel, items, icons, BAND_COUNT + 1);
}

/* ──────────────────────────────────────────────────────────
 * Start Scanning
 * ────────────────────────────────────────────────────────── */
static void start_scan(tool_ctx_t *ctx) {
    subghz_state_t *s = (subghz_state_t *)ctx->state;

    s->freq_start = bands[s->band].start_hz;
    s->freq_end   = bands[s->band].end_hz;
    s->freq_step  = bands[s->band].step_hz;
    s->total_steps = (uint16_t)((s->freq_end - s->freq_start) / s->freq_step);
    if (s->total_steps > 256) s->total_steps = 256;
    s->current_step = 0;
    s->peak_rssi = -128;
    s->peak_freq = 0;
    s->signal_count = 0;
    s->display_counter = 0;
    s->pass = 0;
    s->scan_start_ms = to_ms_since_boot(get_absolute_time());
    memset(s->rssi, -128, sizeof(s->rssi));

    cc1101_reset(s->radio_cs);
    cc1101_write_config(s->radio_cs, scanner_config, SCANNER_CONFIG_COUNT);

    printf("[SUBGHZ] scan start cs=%u band=%s\n", s->radio_cs, bands[s->band].name);
    stdio_flush();
    printf("[SUBGHZ] MDMCFG2=0x%02X MDMCFG4=0x%02X MCSM0=0x%02X\n",
           cc1101_read_reg(s->radio_cs, CC1101_MDMCFG2),
           cc1101_read_reg(s->radio_cs, CC1101_MDMCFG4),
           cc1101_read_reg(s->radio_cs, CC1101_MCSM0));
    printf("[SUBGHZ] FREQ2=0x%02X FREQ1=0x%02X FREQ0=0x%02X (pre-scan)\n",
           cc1101_read_reg(s->radio_cs, CC1101_FREQ2),
           cc1101_read_reg(s->radio_cs, CC1101_FREQ1),
           cc1101_read_reg(s->radio_cs, CC1101_FREQ0));
    stdio_flush();

    cc1101_set_freq(s->radio_cs, s->freq_start);
    cc1101_rx(s->radio_cs);
    sleep_us(1500);
    uint8_t marc = cc1101_read_status(s->radio_cs, CC1101_STATUS_MARCSTATE) & 0x1F;
    int8_t test_rssi = cc1101_read_rssi_dbm(s->radio_cs);
    printf("[SUBGHZ] after SRX: MARCSTATE=0x%02X (want 0x0D=RX) RSSI=%d dBm\n",
           marc, test_rssi);
    printf("[SUBGHZ] FREQ2=0x%02X FREQ1=0x%02X FREQ0=0x%02X (post-set)\n",
           cc1101_read_reg(s->radio_cs, CC1101_FREQ2),
           cc1101_read_reg(s->radio_cs, CC1101_FREQ1),
           cc1101_read_reg(s->radio_cs, CC1101_FREQ0));
    stdio_flush();
    cc1101_idle(s->radio_cs);

    s->mode = SUBGHZ_MODE_SCANNING;
    tool_send_status_bar("Sub-GHz Scan");
    show_scan_progress(s);
}

/* ──────────────────────────────────────────────────────────
 * Single Scan Step
 *
 * Tunes to the next frequency, enters RX, waits for
 * RSSI to settle (~1.5ms), reads the value.
 * ────────────────────────────────────────────────────────── */
static void scan_step(subghz_state_t *s) {
    uint32_t freq = s->freq_start +
                    (uint32_t)s->current_step * s->freq_step;

    cc1101_idle(s->radio_cs);
    cc1101_set_freq(s->radio_cs, freq);
    cc1101_rx(s->radio_cs);

    sleep_us(1500);

    uint8_t marc = cc1101_read_status(s->radio_cs, CC1101_STATUS_MARCSTATE) & 0x1F;
    uint8_t rssi_raw = cc1101_read_status(s->radio_cs, CC1101_STATUS_RSSI);
    int8_t rssi = cc1101_read_rssi_dbm(s->radio_cs);
    if (rssi > s->rssi[s->current_step])
        s->rssi[s->current_step] = rssi;

    printf("[SCAN] %u.%03u MHz  marc=0x%02X  rssi_raw=0x%02X  rssi=%d dBm\n",
           (unsigned)(freq / 1000000),
           (unsigned)((freq % 1000000) / 1000),
           marc, rssi_raw, rssi);
    stdio_flush();

    if (rssi > s->peak_rssi) {
        s->peak_rssi = rssi;
        s->peak_freq = freq;
    }

    cc1101_idle(s->radio_cs);
    s->current_step++;
}

/* ──────────────────────────────────────────────────────────
 * Finish Scan — Detect Peaks
 *
 * Finds local maxima above RSSI_THRESHOLD, sorts by
 * signal strength (strongest first).
 * ────────────────────────────────────────────────────────── */
static void finish_scan(subghz_state_t *s) {
    cc1101_idle(s->radio_cs);
    s->signal_count = 0;

    for (uint16_t i = 0; i < s->total_steps &&
         s->signal_count < MAX_SIGNALS; i++) {
        if (s->rssi[i] < RSSI_THRESHOLD) continue;

        bool is_peak = true;
        if (i > 0 && s->rssi[i - 1] > s->rssi[i])
            is_peak = false;
        if (i < s->total_steps - 1 && s->rssi[i + 1] > s->rssi[i])
            is_peak = false;

        if (is_peak) {
            s->signals[s->signal_count].freq =
                s->freq_start + (uint32_t)i * s->freq_step;
            s->signals[s->signal_count].rssi = s->rssi[i];
            s->signal_count++;
        }
    }

    for (int i = 1; i < s->signal_count; i++) {
        detected_signal_t tmp = s->signals[i];
        int j = i - 1;
        while (j >= 0 && s->signals[j].rssi < tmp.rssi) {
            s->signals[j + 1] = s->signals[j];
            j--;
        }
        s->signals[j + 1] = tmp;
    }

    s->mode = SUBGHZ_MODE_RESULTS;
    s->result_scroll = 0;
    s->result_sel = 0;
    tool_send_status_bar("Sub-GHz Results");
    show_results(s);
}

/* ──────────────────────────────────────────────────────────
 * Display: Scan Progress
 * ────────────────────────────────────────────────────────── */
static void show_scan_progress(subghz_state_t *s) {
    char lb[12][54];
    const char *lp[12];
    int n = 0;

    snprintf(lb[n], 54, "  SUB-GHZ SCANNER");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    snprintf(lb[n], 54, "  Band: %s ISM", bands[s->band].name);
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Range: %u.%02u - %u.%02u MHz",
             (unsigned)(s->freq_start / 1000000),
             (unsigned)((s->freq_start % 1000000) / 10000),
             (unsigned)(s->freq_end / 1000000),
             (unsigned)((s->freq_end % 1000000) / 10000));
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Step:  %u kHz",
             (unsigned)(s->freq_step / 1000));
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - s->scan_start_ms;
    unsigned secs = (unsigned)(elapsed / 1000);
    unsigned total_secs = SCAN_DURATION_MS / 1000;
    snprintf(lb[n], 54, "  Scanning... pass %u  %us/%us",
             s->pass + 1, secs, total_secs);
    lp[n] = lb[n]; n++;

    int bar_len = 30;
    int filled = (int)(elapsed * bar_len / SCAN_DURATION_MS);
    if (filled > bar_len) filled = bar_len;
    char bar[54];
    bar[0] = ' '; bar[1] = ' ';
    for (int i = 0; i < bar_len; i++)
        bar[i + 2] = (i < filled) ? '#' : '.';
    bar[bar_len + 2] = '\0';
    snprintf(lb[n], 54, "%s", bar);
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    if (s->peak_rssi > -128) {
        snprintf(lb[n], 54, "  Peak: %d dBm @ %u.%03u MHz",
                 s->peak_rssi,
                 (unsigned)(s->peak_freq / 1000000),
                 (unsigned)((s->peak_freq % 1000000) / 1000));
    } else {
        snprintf(lb[n], 54, "  Peak: --");
    }
    lp[n] = lb[n]; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ──────────────────────────────────────────────────────────
 * Display: Results
 * ────────────────────────────────────────────────────────── */
static void show_results(subghz_state_t *s) {
    char lb[16][54];
    const char *lp[16];
    int n = 0;

    snprintf(lb[n], 54, "  SCAN RESULTS - %s", bands[s->band].name);
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    if (s->signal_count == 0) {
        lp[n] = "  No signals detected"; n++;
        snprintf(lb[n], 54, "  (threshold: %d dBm)", RSSI_THRESHOLD);
        lp[n] = lb[n]; n++;
    } else {
        snprintf(lb[n], 54, "  %u signal%s detected:",
                 s->signal_count,
                 s->signal_count == 1 ? "" : "s");
        lp[n] = lb[n]; n++;

        lp[n] = ""; n++;

        uint8_t start = s->result_scroll;
        uint8_t visible = 8;
        if (start + visible > s->signal_count)
            visible = s->signal_count - start;

        for (uint8_t i = 0; i < visible; i++) {
            uint8_t idx = start + i;
            detected_signal_t *sig = &s->signals[idx];

            int bar_len = 0;
            if (sig->rssi > RSSI_THRESHOLD) {
                bar_len = (sig->rssi - RSSI_THRESHOLD) * 8
                          / (-RSSI_THRESHOLD);
                if (bar_len > 8) bar_len = 8;
                if (bar_len < 1) bar_len = 1;
            }
            char bar[9];
            for (int b = 0; b < bar_len; b++) bar[b] = '#';
            bar[bar_len] = '\0';

            char marker = (idx == s->result_sel) ? '>' : ' ';
            snprintf(lb[n], 54, " %c%u.%03u MHz  %4d dBm %s",
                     marker,
                     (unsigned)(sig->freq / 1000000),
                     (unsigned)((sig->freq % 1000000) / 1000),
                     sig->rssi, bar);
            lp[n] = lb[n]; n++;
        }

        if (s->signal_count > start + visible) {
            snprintf(lb[n], 54, "  ... %u more",
                     s->signal_count - start - visible);
            lp[n] = lb[n]; n++;
        }
    }

    lp[n] = ""; n++;
    lp[n] = s->signal_count > 0
        ? "  [GREEN] Capture [GRAY] Rescan [RED] Back"
        : "  [GREEN] Rescan  [RED] Back";
    n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ──────────────────────────────────────────────────────────
 * Capture / Replay
 * ────────────────────────────────────────────────────────── */
static void show_capturing(subghz_state_t *s) {
    char lb[8][54];
    const char *lp[8];
    int n = 0;

    lp[n] = "  CAPTURE"; n++;
    lp[n] = ""; n++;
    snprintf(lb[n], 54, "  Freq: %u.%03u MHz",
             (unsigned)(s->capture_freq / 1000000),
             (unsigned)((s->capture_freq % 1000000) / 1000));
    lp[n] = lb[n]; n++;
    lp[n] = ""; n++;
    lp[n] = "  Listening..."; n++;
    lp[n] = "  Press remote now"; n++;
    lp[n] = ""; n++;
    lp[n] = "  [RED] Cancel"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static void do_capture(subghz_state_t *s) {
    uint8_t gdo0 = get_gdo0_pin(s->radio_cs);
    uint32_t last_high = time_us_32();
    uint16_t idx = 0;

    memset(s->capture_data, 0, CAPTURE_MAX_BYTES);

    absolute_time_t next = get_absolute_time();

    while (idx < CAPTURE_MAX_BYTES) {
        uint8_t byte = 0;
        for (int bit = 7; bit >= 0; bit--) {
            next = delayed_by_us(next, CAPTURE_SAMPLE_US);
            busy_wait_until(next);
            if (gpio_get(gdo0)) {
                byte |= (1 << bit);
                last_high = time_us_32();
            }
        }
        s->capture_data[idx++] = byte;

        if (time_us_32() - last_high > CAPTURE_SILENCE_US)
            break;
    }

    s->capture_samples = idx;
}

static void show_captured(subghz_state_t *s) {
    char lb[10][54];
    const char *lp[10];
    int n = 0;

    lp[n] = s->from_saved ? "  SAVED SIGNAL" : "  SIGNAL CAPTURED";
    n++;
    lp[n] = ""; n++;
    snprintf(lb[n], 54, "  Freq:     %u.%03u MHz",
             (unsigned)(s->capture_freq / 1000000),
             (unsigned)((s->capture_freq % 1000000) / 1000));
    lp[n] = lb[n]; n++;
    snprintf(lb[n], 54, "  Size:     %u bytes", s->capture_samples);
    lp[n] = lb[n]; n++;
    uint32_t duration_ms = (uint32_t)s->capture_samples * 8
                           * CAPTURE_SAMPLE_US / 1000;
    snprintf(lb[n], 54, "  Duration: %u ms", (unsigned)duration_ms);
    lp[n] = lb[n]; n++;
    lp[n] = ""; n++;
    if (s->from_saved) {
        lp[n] = "  [GREEN] Replay  [RED] Back"; n++;
    } else {
        lp[n] = "  [GREEN] Save  [BLUE] Replay"; n++;
        lp[n] = "  [GRAY] Recapture  [RED] Back"; n++;
    }

    tool_send_text_screen(lp, (uint8_t)n);
}

static void do_replay(subghz_state_t *s) {
    static const uint8_t patable[2] = { 0x00, 0xC0 };

    cc1101_reset(s->radio_cs);
    cc1101_write_config(s->radio_cs, tx_config, TX_CONFIG_COUNT);
    cc1101_set_freq(s->radio_cs, s->capture_freq);
    cc1101_write_patable(s->radio_cs, patable, 2);

    printf("[SUBGHZ] TX %u bytes at %u.%03u MHz\n",
           s->capture_samples,
           (unsigned)(s->capture_freq / 1000000),
           (unsigned)((s->capture_freq % 1000000) / 1000));
    stdio_flush();

    cc1101_tx(s->radio_cs, s->capture_data, s->capture_samples);

    printf("[SUBGHZ] TX complete\n");
    stdio_flush();
}

static void start_capture(subghz_state_t *s) {
    s->capture_samples = 0;
    s->from_saved = 0;
    s->capture_start_ms = to_ms_since_boot(get_absolute_time());

    cc1101_reset(s->radio_cs);
    cc1101_write_config(s->radio_cs, capture_config, CAPTURE_CONFIG_COUNT);
    cc1101_set_freq(s->radio_cs, s->capture_freq);

    uint8_t gdo0 = get_gdo0_pin(s->radio_cs);
    gpio_init(gdo0);
    gpio_set_dir(gdo0, GPIO_IN);

    cc1101_rx(s->radio_cs);

    s->mode = SUBGHZ_MODE_CAPTURING;
    tool_send_status_bar("Capture");
    show_capturing(s);
}

/* ──────────────────────────────────────────────────────────
 * Filesystem: Save / Load / Browse
 * ────────────────────────────────────────────────────────── */
static void save_capture(subghz_state_t *s) {
    uint32_t mhz = s->capture_freq / 1000000;
    uint32_t khz = (s->capture_freq % 1000000) / 1000;

    char path[64];
    uint16_t seq;
    for (seq = 1; seq < 1000; seq++) {
        snprintf(path, sizeof(path), "/captures/subghz/%u_%03u_%03u.raw",
                 (unsigned)mhz, (unsigned)khz, seq);
        if (!fs_exists(path)) break;
    }

    subghz_file_header_t hdr = {
        .freq = s->capture_freq,
        .samples = s->capture_samples,
        .sample_rate_us = CAPTURE_SAMPLE_US,
    };

    int err = fs_write2(path, &hdr, sizeof(hdr),
                        s->capture_data, s->capture_samples);
    if (err == 0) {
        printf("[SUBGHZ] saved to %s\n", path);
        tool_send_toast("Saved!", 1500);
    } else {
        printf("[SUBGHZ] save failed: %d\n", err);
        tool_send_toast("Save failed!", 2000);
    }
}

static void list_cb(const char *name, uint32_t size, void *user) {
    (void)user;
    if (saved_count < MAX_SAVED_FILES) {
        strncpy(saved_names[saved_count], name, 31);
        saved_names[saved_count][31] = '\0';
        saved_sizes[saved_count] = size;
        saved_count++;
    }
}

static void load_saved_list(subghz_state_t *s) {
    saved_count = 0;
    fs_list("/captures/subghz", list_cb, NULL);
    s->saved_sel = 0;
    s->saved_scroll = 0;
}

static void show_saved_list(subghz_state_t *s) {
    char lb[14][54];
    const char *lp[14];
    int n = 0;

    lp[n] = "  SAVED SIGNALS"; n++;
    lp[n] = ""; n++;

    if (saved_count == 0) {
        lp[n] = "  No saved signals"; n++;
    } else {
        snprintf(lb[n], 54, "  %u file%s:", saved_count,
                 saved_count == 1 ? "" : "s");
        lp[n] = lb[n]; n++;
        lp[n] = ""; n++;

        uint8_t start = s->saved_scroll;
        uint8_t visible = 8;
        if (start + visible > saved_count)
            visible = saved_count - start;

        for (uint8_t i = 0; i < visible; i++) {
            uint8_t idx = start + i;
            char marker = (idx == s->saved_sel) ? '>' : ' ';
            snprintf(lb[n], 54, " %c%-20s %4uB",
                     marker, saved_names[idx],
                     (unsigned)saved_sizes[idx]);
            lp[n] = lb[n]; n++;
        }
    }

    lp[n] = ""; n++;
    lp[n] = saved_count > 0
        ? "  [GREEN] Load  [GRAY] Del  [RED] Back"
        : "  [RED] Back";
    n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static void load_saved_capture(subghz_state_t *s, uint8_t idx) {
    if (idx >= saved_count) return;

    char path[64];
    snprintf(path, sizeof(path), "/captures/subghz/%s", saved_names[idx]);

    int rd = fs_read(path, load_buf, sizeof(load_buf));
    if (rd < (int)sizeof(subghz_file_header_t)) {
        tool_send_toast("Read failed!", 2000);
        return;
    }

    subghz_file_header_t hdr;
    memcpy(&hdr, load_buf, sizeof(hdr));
    s->capture_freq = hdr.freq;
    s->capture_samples = hdr.samples;
    uint16_t data_len = (uint16_t)(rd - (int)sizeof(hdr));
    if (data_len > CAPTURE_MAX_BYTES) data_len = CAPTURE_MAX_BYTES;
    memcpy(s->capture_data, load_buf + sizeof(hdr), data_len);

    s->from_saved = 1;
    s->mode = SUBGHZ_MODE_CAPTURED;
    tool_send_status_bar("Saved Signal");
    show_captured(s);
}

/* ──────────────────────────────────────────────────────────
 * Tool Callbacks
 * ────────────────────────────────────────────────────────── */
static tool_result_t subghz_enter(tool_ctx_t *ctx) {
    subghz_state_t *s = (subghz_state_t *)ctx->state;

    printf("[SUBGHZ] enter: r1=%d r2=%d\n", ctx->hw.radio1_ok, ctx->hw.radio2_ok);
    stdio_flush();

    if (ctx->hw.radio1_ok)
        s->radio_cs = PIN_RADIO1_CS;
    else if (ctx->hw.radio2_ok)
        s->radio_cs = PIN_RADIO2_CS;
    else
        return TOOL_EXIT;

    printf("[SUBGHZ] using cs=%u\n", s->radio_cs);
    stdio_flush();

    s->mode = SUBGHZ_MODE_BAND_SELECT;
    s->menu_sel = 1;
    s->band = 1;
    s->result_sel = 0;
    s->capture_samples = 0;
    s->from_saved = 0;

    tool_send_status_bar("Sub-GHz");
    show_band_menu(s);

    return TOOL_OK;
}

static tool_result_t subghz_update(tool_ctx_t *ctx) {
    subghz_state_t *s = (subghz_state_t *)ctx->state;

    if (s->mode == SUBGHZ_MODE_CAPTURING) {
        uint8_t gdo0 = get_gdo0_pin(s->radio_cs);
        if (gpio_get(gdo0)) {
            do_capture(s);
            if (s->capture_samples >= MIN_CAPTURE_BYTES) {
                cc1101_idle(s->radio_cs);
                s->mode = SUBGHZ_MODE_CAPTURED;
                tool_send_status_bar("Captured");
                show_captured(s);
                printf("[SUBGHZ] captured %u bytes at %u.%03u MHz\n",
                       s->capture_samples,
                       (unsigned)(s->capture_freq / 1000000),
                       (unsigned)((s->capture_freq % 1000000) / 1000));
                stdio_flush();
            }
        }
        uint32_t elapsed = to_ms_since_boot(get_absolute_time())
                           - s->capture_start_ms;
        if (elapsed > CAPTURE_TIMEOUT_MS) {
            cc1101_idle(s->radio_cs);
            tool_send_toast("No signal detected", 2000);
            s->mode = SUBGHZ_MODE_RESULTS;
            tool_send_status_bar("Sub-GHz Results");
            show_results(s);
        }
        return TOOL_OK;
    }

    if (s->mode != SUBGHZ_MODE_SCANNING) return TOOL_OK;

    if (s->current_step < s->total_steps) {
        scan_step(s);
        s->display_counter++;

        if (s->display_counter >= DISPLAY_INTERVAL ||
            s->current_step >= s->total_steps) {
            s->display_counter = 0;
            show_scan_progress(s);
        }
    }

    if (s->current_step >= s->total_steps) {
        uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - s->scan_start_ms;
        if (elapsed < SCAN_DURATION_MS) {
            s->current_step = 0;
            s->pass++;
            printf("[SUBGHZ] pass %u done (%u ms elapsed), continuing...\n",
                   s->pass, (unsigned)elapsed);
            stdio_flush();
        } else {
            printf("[SUBGHZ] scan complete: %u passes, %u ms\n",
                   s->pass + 1, (unsigned)elapsed);
            stdio_flush();
            finish_scan(s);
        }
    }

    return TOOL_OK;
}

static tool_result_t subghz_on_button(tool_ctx_t *ctx,
                                       uint8_t btn_id,
                                       uint8_t btn_state) {
    subghz_state_t *s = (subghz_state_t *)ctx->state;

    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (s->mode) {
    case SUBGHZ_MODE_BAND_SELECT:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->menu_sel > 0) s->menu_sel--;
            else s->menu_sel = BAND_COUNT;
            show_band_menu(s);
            break;
        case IPP_BTN_BLUE:
            if (s->menu_sel < BAND_COUNT) s->menu_sel++;
            else s->menu_sel = 0;
            show_band_menu(s);
            break;
        case IPP_BTN_GREEN:
            if (s->menu_sel == BAND_COUNT) {
                load_saved_list(s);
                s->mode = SUBGHZ_MODE_SAVED_LIST;
                tool_send_status_bar("Saved Signals");
                show_saved_list(s);
            } else {
                s->band = s->menu_sel;
                start_scan(ctx);
            }
            break;
        case IPP_BTN_RED:
            return TOOL_EXIT;
        }
        break;

    case SUBGHZ_MODE_SCANNING:
        if (btn_id == IPP_BTN_RED) {
            cc1101_idle(s->radio_cs);
            s->mode = SUBGHZ_MODE_BAND_SELECT;
            tool_send_status_bar("Sub-GHz");
            show_band_menu(s);
        }
        break;

    case SUBGHZ_MODE_RESULTS:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->signal_count > 0 && s->result_sel > 0) {
                s->result_sel--;
                if (s->result_sel < s->result_scroll)
                    s->result_scroll = s->result_sel;
                show_results(s);
            }
            break;
        case IPP_BTN_BLUE:
            if (s->signal_count > 0 &&
                s->result_sel < s->signal_count - 1) {
                s->result_sel++;
                if (s->result_sel >= s->result_scroll + 8)
                    s->result_scroll = s->result_sel - 7;
                show_results(s);
            }
            break;
        case IPP_BTN_GREEN:
            if (s->signal_count > 0) {
                s->selected_signal = s->result_sel;
                s->capture_freq = s->signals[s->selected_signal].freq;
                start_capture(s);
            } else {
                start_scan(ctx);
            }
            break;
        case IPP_BTN_GRAY:
            start_scan(ctx);
            break;
        case IPP_BTN_RED:
            s->mode = SUBGHZ_MODE_BAND_SELECT;
            tool_send_status_bar("Sub-GHz");
            show_band_menu(s);
            break;
        }
        break;

    case SUBGHZ_MODE_CAPTURING:
        if (btn_id == IPP_BTN_RED) {
            cc1101_idle(s->radio_cs);
            s->mode = SUBGHZ_MODE_RESULTS;
            tool_send_status_bar("Sub-GHz Results");
            show_results(s);
        }
        break;

    case SUBGHZ_MODE_CAPTURED:
        switch (btn_id) {
        case IPP_BTN_GREEN:
            if (s->from_saved) {
                tool_send_toast("Transmitting...", 1000);
                do_replay(s);
                tool_send_toast("Transmitted!", 1500);
                show_captured(s);
            } else {
                save_capture(s);
                show_captured(s);
            }
            break;
        case IPP_BTN_BLUE:
            if (!s->from_saved) {
                tool_send_toast("Transmitting...", 1000);
                do_replay(s);
                tool_send_toast("Transmitted!", 1500);
                show_captured(s);
            }
            break;
        case IPP_BTN_GRAY:
            if (!s->from_saved)
                start_capture(s);
            break;
        case IPP_BTN_RED:
            if (s->from_saved) {
                load_saved_list(s);
                s->mode = SUBGHZ_MODE_SAVED_LIST;
                tool_send_status_bar("Saved Signals");
                show_saved_list(s);
            } else {
                s->mode = SUBGHZ_MODE_RESULTS;
                tool_send_status_bar("Sub-GHz Results");
                show_results(s);
            }
            break;
        }
        break;

    case SUBGHZ_MODE_SAVED_LIST:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (saved_count > 0 && s->saved_sel > 0) {
                s->saved_sel--;
                if (s->saved_sel < s->saved_scroll)
                    s->saved_scroll = s->saved_sel;
                show_saved_list(s);
            }
            break;
        case IPP_BTN_BLUE:
            if (saved_count > 0 &&
                s->saved_sel < saved_count - 1) {
                s->saved_sel++;
                if (s->saved_sel >= s->saved_scroll + 8)
                    s->saved_scroll = s->saved_sel - 7;
                show_saved_list(s);
            }
            break;
        case IPP_BTN_GREEN:
            if (saved_count > 0)
                load_saved_capture(s, s->saved_sel);
            break;
        case IPP_BTN_GRAY:
            if (saved_count > 0) {
                char path[64];
                snprintf(path, sizeof(path), "/captures/subghz/%s",
                         saved_names[s->saved_sel]);
                fs_delete(path);
                tool_send_toast("Deleted", 1000);
                load_saved_list(s);
                show_saved_list(s);
            }
            break;
        case IPP_BTN_RED:
            s->mode = SUBGHZ_MODE_BAND_SELECT;
            tool_send_status_bar("Sub-GHz");
            show_band_menu(s);
            break;
        }
        break;
    }

    return TOOL_OK;
}

static void subghz_exit(tool_ctx_t *ctx) {
    subghz_state_t *s = (subghz_state_t *)ctx->state;
    cc1101_idle(s->radio_cs);
}

/* ──────────────────────────────────────────────────────────
 * Tool Descriptor
 * ────────────────────────────────────────────────────────── */
const tool_desc_t tool_subghz_desc = {
    .name         = "Sub-GHz",
    .icon_id      = 1,
    .requires     = REQUIRE_RADIO_ANY,
    .state_size   = sizeof(subghz_state_t),
    .enter        = subghz_enter,
    .update       = subghz_update,
    .on_button    = subghz_on_button,
    .exit         = subghz_exit,
    .on_orca_msg  = NULL,
};
