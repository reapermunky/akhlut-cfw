/**
 * tool_settings.c — Settings Tool
 *
 * Akhlut CFW
 *
 * Adjustable device settings: backlight brightness,
 * LED enable, auto-sleep timeout. Settings are persisted
 * to the last sector of flash and loaded on tool enter.
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>

#define SETTINGS_COUNT  3

#define SETTINGS_MAGIC   0x41434657  // "ACFW"
#define SETTINGS_VERSION 1
#define SETTINGS_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t  version;
    uint8_t  brightness;
    uint8_t  leds_on;
    uint8_t  sleep_min;
    uint16_t crc;
} settings_flash_t;

typedef struct {
    uint8_t selected;
    uint8_t brightness;
    bool    leds_on;
    uint8_t sleep_min;
} settings_state_t;

_Static_assert(sizeof(settings_state_t) <= TOOL_STATE_POOL_SIZE,
               "settings_state_t exceeds state pool");
_Static_assert(sizeof(settings_flash_t) <= FLASH_PAGE_SIZE,
               "settings_flash_t exceeds flash page");

static const uint8_t brightness_steps[] = { 0, 80, 150, 200, 255 };
#define BRIGHTNESS_STEP_COUNT \
    (sizeof(brightness_steps) / sizeof(brightness_steps[0]))

static const uint8_t sleep_options[] = { 0, 1, 5, 10, 30 };
#define SLEEP_OPTION_COUNT \
    (sizeof(sleep_options) / sizeof(sleep_options[0]))

static int find_brightness_idx(uint8_t val) {
    for (int i = 0; i < (int)BRIGHTNESS_STEP_COUNT; i++) {
        if (brightness_steps[i] >= val) return i;
    }
    return BRIGHTNESS_STEP_COUNT - 1;
}

static int find_sleep_idx(uint8_t val) {
    for (int i = 0; i < (int)SLEEP_OPTION_COUNT; i++) {
        if (sleep_options[i] >= val) return i;
    }
    return SLEEP_OPTION_COUNT - 1;
}

static uint8_t cached_brightness = 200;
static uint8_t cached_sleep_min = 0;

static void apply_leds(bool on);

uint8_t settings_get_brightness(void) { return cached_brightness; }
uint8_t settings_get_sleep_min(void) { return cached_sleep_min; }

/* ──────────────────────────────────────────────────────────
 * Flash Persistence
 * ────────────────────────────────────────────────────────── */
static uint16_t settings_crc(const settings_flash_t *sf) {
    uint16_t crc = 0xFFFF;
    const uint8_t *p = (const uint8_t *)sf;
    size_t len = offsetof(settings_flash_t, crc);
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)p[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

static bool settings_load(settings_state_t *s) {
    const settings_flash_t *sf =
        (const settings_flash_t *)(XIP_BASE + SETTINGS_FLASH_OFFSET);

    if (sf->magic != SETTINGS_MAGIC) {
        printf("[Settings] Load: bad magic 0x%08X\n", sf->magic);
        return false;
    }
    if (sf->version != SETTINGS_VERSION) {
        printf("[Settings] Load: bad version %d\n", sf->version);
        return false;
    }
    if (settings_crc(sf) != sf->crc) {
        printf("[Settings] Load: CRC mismatch\n");
        return false;
    }

    s->brightness = sf->brightness;
    s->leds_on    = sf->leds_on;
    s->sleep_min  = sf->sleep_min;
    return true;
}

typedef struct {
    settings_flash_t data;
} flash_write_ctx_t;

static void settings_flash_write_cb(void *param) {
    flash_write_ctx_t *ctx = (flash_write_ctx_t *)param;
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &ctx->data, sizeof(ctx->data));

    flash_range_erase(SETTINGS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SETTINGS_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
}

static void settings_save(const settings_state_t *s) {
    flash_write_ctx_t ctx;
    ctx.data.magic      = SETTINGS_MAGIC;
    ctx.data.version    = SETTINGS_VERSION;
    ctx.data.brightness = s->brightness;
    ctx.data.leds_on    = s->leds_on ? 1 : 0;
    ctx.data.sleep_min  = s->sleep_min;
    ctx.data.crc        = settings_crc(&ctx.data);

    printf("[Settings] Saving: bright=%d leds=%d sleep=%d\n",
           s->brightness, s->leds_on ? 1 : 0, s->sleep_min);
    int rc = flash_safe_execute(settings_flash_write_cb, &ctx, 500);
    if (rc == PICO_OK) {
        printf("[Settings] Flash write OK\n");
    } else {
        printf("[Settings] Flash write FAILED: %d\n", rc);
    }
}

/* ──────────────────────────────────────────────────────────
 * Boot-Time Apply
 * ────────────────────────────────────────────────────────── */
void settings_boot_apply(void) {
    settings_state_t s;
    s.brightness = 200;
    s.leds_on = true;
    s.sleep_min = 0;

    if (settings_load(&s)) {
        printf("[Settings] Loaded: bright=%d leds=%d sleep=%d\n",
               s.brightness, s.leds_on, s.sleep_min);
    } else {
        printf("[Settings] No saved settings, using defaults\n");
    }

    cached_brightness = s.brightness;
    cached_sleep_min = s.sleep_min;

    ipp_backlight_t bl;
    bl.brightness = s.brightness;
    tool_send_display(IPP_MSG_BACKLIGHT, &bl, sizeof(bl));

    apply_leds(s.leds_on);
}

/* ──────────────────────────────────────────────────────────
 * Display
 * ────────────────────────────────────────────────────────── */
static void apply_backlight(settings_state_t *s) {
    ipp_backlight_t bl;
    bl.brightness = s->brightness;
    tool_send_display(IPP_MSG_BACKLIGHT, &bl, sizeof(bl));
}

static void apply_leds(bool on) {
    for (uint8_t i = 0; i < 7; i++) {
        ipp_led_set_t ls = { .led_index = i,
                             .r = 0, .g = on ? 2 : 0, .b = 0 };
        tool_send_display(IPP_MSG_LED_SET, &ls, sizeof(ls));
    }
}

static void show_settings(settings_state_t *s) {
    char lb[10][54];
    const char *lp[10];
    int n = 0;

    snprintf(lb[n], 54, "  SETTINGS");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    static const char *br_labels[] = { "Off", "25%", "50%", "75%", "100%" };
    int br_idx = find_brightness_idx(s->brightness);
    snprintf(lb[n], 54, "%sBacklight:    %s",
             s->selected == 0 ? "> " : "  ", br_labels[br_idx]);
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "%sLEDs:         %s",
             s->selected == 1 ? "> " : "  ", s->leds_on ? "On" : "Off");
    lp[n] = lb[n]; n++;

    if (s->sleep_min == 0) {
        snprintf(lb[n], 54, "%sAuto-sleep:   Never",
                 s->selected == 2 ? "> " : "  ");
    } else {
        snprintf(lb[n], 54, "%sAuto-sleep:   %u min",
                 s->selected == 2 ? "> " : "  ", s->sleep_min);
    }
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;
    lp[n] = "  [GREEN] Change value"; n++;
    lp[n] = "  [RED] Back"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ──────────────────────────────────────────────────────────
 * Tool Callbacks
 * ────────────────────────────────────────────────────────── */
static tool_result_t settings_enter(tool_ctx_t *ctx) {
    settings_state_t *s = (settings_state_t *)ctx->state;

    s->selected = 0;
    s->brightness = 192;
    s->leds_on = true;
    s->sleep_min = 5;

    if (settings_load(s)) {
        apply_backlight(s);
    }

    tool_send_status_bar("Settings");
    show_settings(s);

    return TOOL_OK;
}

static tool_result_t settings_update(tool_ctx_t *ctx) {
    (void)ctx;
    return TOOL_OK;
}

static tool_result_t settings_on_button(tool_ctx_t *ctx,
                                         uint8_t btn_id,
                                         uint8_t btn_state) {
    settings_state_t *s = (settings_state_t *)ctx->state;

    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (btn_id) {
    case IPP_BTN_YELLOW:
        if (s->selected > 0) s->selected--;
        else s->selected = SETTINGS_COUNT - 1;
        show_settings(s);
        break;

    case IPP_BTN_BLUE:
        if (s->selected < SETTINGS_COUNT - 1) s->selected++;
        else s->selected = 0;
        show_settings(s);
        break;

    case IPP_BTN_GREEN:
        switch (s->selected) {
        case 0: {
            int idx = find_brightness_idx(s->brightness);
            idx = (idx + 1) % BRIGHTNESS_STEP_COUNT;
            s->brightness = brightness_steps[idx];
            apply_backlight(s);
            break;
        }
        case 1:
            s->leds_on = !s->leds_on;
            apply_leds(s->leds_on);
            break;
        case 2: {
            int idx = find_sleep_idx(s->sleep_min);
            idx = (idx + 1) % SLEEP_OPTION_COUNT;
            s->sleep_min = sleep_options[idx];
            break;
        }
        }
        cached_brightness = s->brightness;
        cached_sleep_min = s->sleep_min;
        settings_save(s);
        show_settings(s);
        break;

    case IPP_BTN_RED:
        return TOOL_EXIT;
    }

    return TOOL_OK;
}

static void settings_exit(tool_ctx_t *ctx) {
    (void)ctx;
}

const tool_desc_t tool_settings_desc = {
    .name         = "Settings",
    .icon_id      = 7,
    .requires     = 0,
    .state_size   = sizeof(settings_state_t),
    .enter        = settings_enter,
    .update       = settings_update,
    .on_button    = settings_on_button,
    .exit         = settings_exit,
    .on_orca_msg  = NULL,
};
