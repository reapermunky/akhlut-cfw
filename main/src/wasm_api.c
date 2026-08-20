/**
 * wasm_api.c — WASM App Runtime
 *
 * Akhlut CFW
 *
 * Loads .wasm files from flash filesystem, runs them in the
 * wasm3 interpreter, and exposes host functions for display,
 * input, radio, GPIO, storage, LEDs, and system control.
 *
 * All display calls go through IPP to the Display RP2040.
 * WASM apps run on Core 0 in the tool engine's update loop.
 */

#include "wasm_api.h"
#include "wasm3.h"
#include "tool.h"
#include "board.h"
#include "cc1101.h"
#include "fs.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static wasm_ctx_t *g_wasm_ctx = NULL;

extern volatile bool tool_wasm_force_stop;

extern volatile uint8_t  battery_pct;
extern volatile uint16_t battery_vbatt_mv;
extern volatile uint8_t  battery_flags_raw;
extern volatile bool     ir_pending;
extern volatile ipp_ir_code_t ir_pending_code;
extern volatile bool     accel_data_valid;
extern volatile ipp_accel_data_t accel_last;
extern volatile bool     ioexp_resp_pending;
extern volatile uint8_t  ioexp_resp_reg;
extern volatile uint8_t  ioexp_resp_val;
extern volatile uint8_t  ioexp_resp_status;

/* ──────────────────────────────────────────────────────────
 * Force-stop check helper
 * ────────────────────────────────────────────────────────── */
#define WASM_CHECK_STOP() do { \
    if (tool_wasm_force_stop) { \
        if (g_wasm_ctx) g_wasm_ctx->exit_requested = true; \
        m3ApiTrap(m3Err_trapExit); \
    } \
} while(0)

/* ──────────────────────────────────────────────────────────
 * Display Host Functions — Original
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_clear_screen) {
    (void)runtime; (void)_ctx; (void)_sp; (void)_mem;
    tool_send_screen_clear();
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_draw_text) {
    m3ApiGetArg(uint32_t, x);
    m3ApiGetArg(uint32_t, y);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(uint32_t, text_len);
    m3ApiGetArg(uint32_t, color);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (text_offset >= mem_size || text_len > mem_size - text_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    const char *text = (const char *)((uint8_t *)_mem + text_offset);

    uint8_t payload[256];
    ipp_draw_text_header_t *hdr = (ipp_draw_text_header_t *)payload;
    hdr->x = (uint16_t)x;
    hdr->y = (uint16_t)y;
    hdr->font_id = 1;
    hdr->color = (uint16_t)color;

    uint32_t copy_len = text_len;
    if (copy_len > sizeof(payload) - sizeof(ipp_draw_text_header_t) - 1)
        copy_len = sizeof(payload) - sizeof(ipp_draw_text_header_t) - 1;

    memcpy(payload + sizeof(ipp_draw_text_header_t), text, copy_len);
    payload[sizeof(ipp_draw_text_header_t) + copy_len] = '\0';

    tool_send_display(IPP_MSG_DRAW_TEXT, payload,
                      (uint16_t)(sizeof(ipp_draw_text_header_t) + copy_len + 1));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_draw_rect) {
    m3ApiGetArg(uint32_t, x);
    m3ApiGetArg(uint32_t, y);
    m3ApiGetArg(uint32_t, w);
    m3ApiGetArg(uint32_t, h);
    m3ApiGetArg(uint32_t, color);
    m3ApiGetArg(uint32_t, filled);

    ipp_draw_rect_t rect = {
        .x = (uint16_t)x, .y = (uint16_t)y,
        .w = (uint16_t)w, .h = (uint16_t)h,
        .color = (uint16_t)color,
        .filled = (uint8_t)filled,
    };
    tool_send_display(IPP_MSG_DRAW_RECT, &rect, sizeof(rect));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_draw_line) {
    m3ApiGetArg(uint32_t, x0);
    m3ApiGetArg(uint32_t, y0);
    m3ApiGetArg(uint32_t, x1);
    m3ApiGetArg(uint32_t, y1);
    m3ApiGetArg(uint32_t, color);

    ipp_draw_line_t line = {
        .x0 = (uint16_t)x0, .y0 = (uint16_t)y0,
        .x1 = (uint16_t)x1, .y1 = (uint16_t)y1,
        .color = (uint16_t)color,
    };
    tool_send_display(IPP_MSG_DRAW_LINE, &line, sizeof(line));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_fb_flip) {
    (void)runtime; (void)_ctx; (void)_sp; (void)_mem;
    tool_send_display(IPP_MSG_FB_FLIP, NULL, 0);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_toast) {
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(uint32_t, text_len);
    m3ApiGetArg(uint32_t, duration_ms);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (text_offset >= mem_size || text_len > mem_size - text_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    const char *text = (const char *)((uint8_t *)_mem + text_offset);
    char buf[64];
    uint32_t clen = text_len < 63 ? text_len : 63;
    memcpy(buf, text, clen);
    buf[clen] = '\0';

    tool_send_toast(buf, (uint16_t)duration_ms);
    m3ApiSuccess();
}

/* ──────────────────────────────────────────────────────────
 * Display Host Functions — New
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_draw_text_small) {
    m3ApiGetArg(uint32_t, x);
    m3ApiGetArg(uint32_t, y);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(uint32_t, text_len);
    m3ApiGetArg(uint32_t, color);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (text_offset >= mem_size || text_len > mem_size - text_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    const char *text = (const char *)((uint8_t *)_mem + text_offset);

    uint8_t payload[256];
    ipp_draw_text_header_t *hdr = (ipp_draw_text_header_t *)payload;
    hdr->x = (uint16_t)x;
    hdr->y = (uint16_t)y;
    hdr->font_id = 0;
    hdr->color = (uint16_t)color;

    uint32_t copy_len = text_len;
    if (copy_len > sizeof(payload) - sizeof(ipp_draw_text_header_t) - 1)
        copy_len = sizeof(payload) - sizeof(ipp_draw_text_header_t) - 1;

    memcpy(payload + sizeof(ipp_draw_text_header_t), text, copy_len);
    payload[sizeof(ipp_draw_text_header_t) + copy_len] = '\0';

    tool_send_display(IPP_MSG_DRAW_TEXT, payload,
                      (uint16_t)(sizeof(ipp_draw_text_header_t) + copy_len + 1));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_set_pixel) {
    m3ApiGetArg(uint32_t, x);
    m3ApiGetArg(uint32_t, y);
    m3ApiGetArg(uint32_t, color);

    ipp_draw_rect_t rect = {
        .x = (uint16_t)x, .y = (uint16_t)y,
        .w = 1, .h = 1,
        .color = (uint16_t)color,
        .filled = 1,
    };
    tool_send_display(IPP_MSG_DRAW_RECT, &rect, sizeof(rect));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_draw_circle) {
    m3ApiGetArg(uint32_t, cx);
    m3ApiGetArg(uint32_t, cy);
    m3ApiGetArg(uint32_t, r);
    m3ApiGetArg(uint32_t, color);
    m3ApiGetArg(uint32_t, filled);

    ipp_draw_circle_t circ = {
        .cx = (uint16_t)cx, .cy = (uint16_t)cy,
        .r = (uint16_t)r,
        .color = (uint16_t)color,
        .filled = (uint8_t)filled,
    };
    tool_send_display(IPP_MSG_DRAW_CIRCLE, &circ, sizeof(circ));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_set_backlight) {
    m3ApiGetArg(uint32_t, brightness);

    ipp_backlight_t bl = { .brightness = (uint8_t)brightness };
    tool_send_display(IPP_MSG_BACKLIGHT, &bl, sizeof(bl));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_progress) {
    m3ApiGetArg(uint32_t, percent);
    m3ApiGetArg(uint32_t, text_offset);
    m3ApiGetArg(uint32_t, text_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (text_offset >= mem_size || text_len > mem_size - text_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    uint8_t payload[128];
    ipp_progress_t *hdr = (ipp_progress_t *)payload;
    hdr->percent = (uint8_t)percent;

    uint32_t copy_len = text_len;
    if (copy_len > sizeof(payload) - sizeof(ipp_progress_t) - 1)
        copy_len = sizeof(payload) - sizeof(ipp_progress_t) - 1;

    memcpy(payload + sizeof(ipp_progress_t),
           (uint8_t *)_mem + text_offset, copy_len);
    payload[sizeof(ipp_progress_t) + copy_len] = '\0';

    tool_send_display(IPP_MSG_PROGRESS, payload,
                      (uint16_t)(sizeof(ipp_progress_t) + copy_len + 1));
    m3ApiSuccess();
}

/* ──────────────────────────────────────────────────────────
 * Input Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_get_button) {
    m3ApiReturnType(uint32_t);

    uint8_t btn;
    if (tool_poll_button(&btn))
        m3ApiReturn((uint32_t)btn);
    WASM_CHECK_STOP();
    m3ApiReturn(0xFF);
}

m3ApiRawFunction(m3_app_wait_button) {
    m3ApiReturnType(uint32_t);

    uint8_t btn;
    while (!tool_poll_button(&btn)) {
        if (tool_wasm_force_stop || (g_wasm_ctx && g_wasm_ctx->exit_requested)) {
            if (g_wasm_ctx) g_wasm_ctx->exit_requested = true;
            m3ApiTrap(m3Err_trapExit);
        }
        tight_loop_contents();
        sleep_ms(10);
    }
    m3ApiReturn((uint32_t)btn);
}

/* ──────────────────────────────────────────────────────────
 * Radio Host Functions — Radio 1 (original + new)
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_radio_set_freq) {
    m3ApiGetArg(uint32_t, freq_hz);

    cc1101_idle(PIN_RADIO1_CS);
    cc1101_set_freq(PIN_RADIO1_CS, freq_hz);
    cc1101_rx(PIN_RADIO1_CS);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_radio_get_rssi) {
    m3ApiReturnType(int32_t);
    int8_t rssi = cc1101_read_rssi_dbm(PIN_RADIO1_CS);
    m3ApiReturn((int32_t)rssi);
}

m3ApiRawFunction(m3_app_radio_tx) {
    m3ApiGetArg(uint32_t, data_offset);
    m3ApiGetArg(uint32_t, data_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (data_offset >= mem_size || data_len > mem_size - data_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (data_len > 64) data_len = 64;

    const uint8_t *data = (const uint8_t *)_mem + data_offset;
    cc1101_tx(PIN_RADIO1_CS, data, (uint16_t)data_len);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_radio_rx) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, max_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || max_len > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (max_len > 64) max_len = 64;

    uint8_t *buf = (uint8_t *)_mem + buf_offset;
    int n = cc1101_rx_read(PIN_RADIO1_CS, buf, (uint8_t)max_len);
    m3ApiReturn((int32_t)n);
}

m3ApiRawFunction(m3_app_radio_set_config) {
    m3ApiGetArg(uint32_t, modulation);
    m3ApiGetArg(uint32_t, data_rate);
    m3ApiGetArg(uint32_t, rx_bw);

    cc1101_idle(PIN_RADIO1_CS);

    // MDMCFG2: MOD_FORMAT in bits [6:4], sync mode 16/16 in [2:0]
    uint8_t mod = (modulation > 7) ? 0 : (uint8_t)modulation;
    cc1101_write_reg(PIN_RADIO1_CS, CC1101_MDMCFG2, (mod << 4) | 0x02);

    // Compute DRATE_E and DRATE_M from baud rate
    if (data_rate > 0) {
        uint64_t ratio = ((uint64_t)data_rate << 28) / CC1101_XOSC_FREQ;
        uint8_t drate_e = 0;
        while (drate_e < 15 && ratio >= (512ULL << drate_e))
            drate_e++;
        uint32_t m_val = (uint32_t)(ratio >> drate_e);
        uint8_t drate_m = (m_val >= 256) ? (uint8_t)(m_val - 256) : 0;

        // Find closest RX bandwidth setting
        uint8_t bw_bits = 0;
        if (rx_bw > 0) {
            uint32_t best_diff = 0xFFFFFFFF;
            for (uint8_t e = 0; e < 4; e++) {
                for (uint8_t m2 = 0; m2 < 4; m2++) {
                    uint32_t bw = CC1101_XOSC_FREQ / (8 * (4 + m2) * (1u << e));
                    uint32_t diff = (bw > rx_bw) ? bw - rx_bw : rx_bw - bw;
                    if (diff < best_diff) {
                        best_diff = diff;
                        bw_bits = (e << 6) | (m2 << 4);
                    }
                }
            }
        }

        cc1101_write_reg(PIN_RADIO1_CS, CC1101_MDMCFG4,
                         bw_bits | (drate_e & 0x0F));
        cc1101_write_reg(PIN_RADIO1_CS, CC1101_MDMCFG3, drate_m);
    }

    cc1101_rx(PIN_RADIO1_CS);
    m3ApiSuccess();
}

/* ──────────────────────────────────────────────────────────
 * Radio Host Functions — Radio 2
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_radio2_set_freq) {
    m3ApiGetArg(uint32_t, freq_hz);

    cc1101_idle(PIN_RADIO2_CS);
    cc1101_set_freq(PIN_RADIO2_CS, freq_hz);
    cc1101_rx(PIN_RADIO2_CS);
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_radio2_get_rssi) {
    m3ApiReturnType(int32_t);
    int8_t rssi = cc1101_read_rssi_dbm(PIN_RADIO2_CS);
    m3ApiReturn((int32_t)rssi);
}

m3ApiRawFunction(m3_app_radio2_tx) {
    m3ApiGetArg(uint32_t, data_offset);
    m3ApiGetArg(uint32_t, data_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (data_offset >= mem_size || data_len > mem_size - data_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (data_len > 64) data_len = 64;

    const uint8_t *data = (const uint8_t *)_mem + data_offset;
    cc1101_tx(PIN_RADIO2_CS, data, (uint16_t)data_len);
    m3ApiSuccess();
}

/* ──────────────────────────────────────────────────────────
 * GPIO Host Functions
 * ────────────────────────────────────────────────────────── */

static bool gpio_pin_allowed(uint32_t pin) {
    return pin == PIN_EXT_GPIO_24 || pin == PIN_EXT_GPIO_25 ||
           pin == PIN_EXT_GPIO_26;
}

m3ApiRawFunction(m3_app_gpio_read) {
    m3ApiReturnType(uint32_t);
    m3ApiGetArg(uint32_t, pin);

    if (!gpio_pin_allowed(pin))
        m3ApiReturn(0);

    m3ApiReturn(gpio_get(pin) ? 1 : 0);
}

m3ApiRawFunction(m3_app_gpio_write) {
    m3ApiGetArg(uint32_t, pin);
    m3ApiGetArg(uint32_t, value);

    if (gpio_pin_allowed(pin)) {
        gpio_set_dir(pin, GPIO_OUT);
        gpio_put(pin, value != 0);
    }
    m3ApiSuccess();
}

/* ──────────────────────────────────────────────────────────
 * Storage Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_fs_write) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, path_offset);
    m3ApiGetArg(uint32_t, path_len);
    m3ApiGetArg(uint32_t, data_offset);
    m3ApiGetArg(uint32_t, data_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (path_offset >= mem_size || path_len > mem_size - path_offset ||
        data_offset >= mem_size || data_len > mem_size - data_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    char path[64];
    uint32_t plen = path_len < 63 ? path_len : 63;
    memcpy(path, (uint8_t *)_mem + path_offset, plen);
    path[plen] = '\0';

    if (strncmp(path, "/apps/", 6) != 0)
        m3ApiReturn(-1);

    const void *data = (uint8_t *)_mem + data_offset;
    int err = fs_write(path, data, data_len);
    m3ApiReturn(err);
}

m3ApiRawFunction(m3_app_fs_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, path_offset);
    m3ApiGetArg(uint32_t, path_len);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, buf_max);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (path_offset >= mem_size || path_len > mem_size - path_offset ||
        buf_offset >= mem_size || buf_max > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    char path[64];
    uint32_t plen = path_len < 63 ? path_len : 63;
    memcpy(path, (uint8_t *)_mem + path_offset, plen);
    path[plen] = '\0';

    if (strncmp(path, "/apps/", 6) != 0)
        m3ApiReturn(-1);

    void *buf = (uint8_t *)_mem + buf_offset;
    int rd = fs_read(path, buf, buf_max);
    m3ApiReturn(rd);
}

m3ApiRawFunction(m3_app_fs_delete) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, path_offset);
    m3ApiGetArg(uint32_t, path_len);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (path_offset >= mem_size || path_len > mem_size - path_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    char path[64];
    uint32_t plen = path_len < 63 ? path_len : 63;
    memcpy(path, (uint8_t *)_mem + path_offset, plen);
    path[plen] = '\0';

    if (strncmp(path, "/apps/", 6) != 0)
        m3ApiReturn(-1);

    int err = fs_delete(path);
    m3ApiReturn(err);
}

typedef struct {
    uint8_t *buf;
    uint32_t max;
    uint32_t pos;
    uint32_t count;
} fs_list_adapter_t;

static void fs_list_wasm_cb(const char *name, uint32_t size, void *user) {
    (void)size;
    fs_list_adapter_t *a = (fs_list_adapter_t *)user;
    uint32_t nlen = strlen(name);
    if (a->pos + nlen + 1 <= a->max) {
        memcpy(a->buf + a->pos, name, nlen);
        a->buf[a->pos + nlen] = '\0';
        a->pos += nlen + 1;
    }
    a->count++;
}

m3ApiRawFunction(m3_app_fs_list) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, buf_max);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || buf_max > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    fs_list_adapter_t a = {
        .buf = (uint8_t *)_mem + buf_offset,
        .max = buf_max,
        .pos = 0,
        .count = 0,
    };

    fs_list("/apps", fs_list_wasm_cb, &a);
    m3ApiReturn((int32_t)a.count);
}

/* ──────────────────────────────────────────────────────────
 * LED Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_set_led) {
    m3ApiGetArg(uint32_t, index);
    m3ApiGetArg(uint32_t, r);
    m3ApiGetArg(uint32_t, g);
    m3ApiGetArg(uint32_t, b);

    ipp_led_set_t led = {
        .led_index = (uint8_t)index,
        .r = (uint8_t)r, .g = (uint8_t)g, .b = (uint8_t)b,
    };
    tool_send_display(IPP_MSG_LED_SET, &led, sizeof(led));
    m3ApiSuccess();
}

/* ──────────────────────────────────────────────────────────
 * Sensor Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_get_accel) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || 10 > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    if (!accel_data_valid)
        m3ApiReturn(-1);

    ipp_accel_data_t snap;
    memcpy(&snap, (const void *)&accel_last, sizeof(snap));

    uint8_t *out = (uint8_t *)_mem + buf_offset;
    memcpy(out, &snap, sizeof(snap));
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_app_get_time) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    (void)buf_offset;
    m3ApiReturn(-1);
}

m3ApiRawFunction(m3_app_get_battery_mv) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn((uint32_t)battery_vbatt_mv);
}

m3ApiRawFunction(m3_app_get_charging) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn((uint32_t)battery_flags_raw);
}

m3ApiRawFunction(m3_app_ioexp_write) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, reg);
    m3ApiGetArg(uint32_t, value);

    uint8_t payload[2] = { (uint8_t)reg, (uint8_t)value };
    tool_send_display(IPP_MSG_IOEXP_WRITE, payload, 2);
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_app_ioexp_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, reg);

    ioexp_resp_pending = false;
    uint8_t payload = (uint8_t)reg;
    tool_send_display(IPP_MSG_IOEXP_READ, &payload, 1);

    uint32_t timeout = 200;
    while (timeout > 0) {
        WASM_CHECK_STOP();
        if (ioexp_resp_pending) {
            if (ioexp_resp_reg == (uint8_t)reg)
                break;
            ioexp_resp_pending = false;
        }
        sleep_ms(1);
        uint8_t dummy;
        tool_poll_button(&dummy);
        timeout--;
    }

    if (!ioexp_resp_pending || ioexp_resp_status != 0)
        m3ApiReturn(-1);

    m3ApiReturn((int32_t)ioexp_resp_val);
}

/* ──────────────────────────────────────────────────────────
 * IR Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_ir_send) {
    m3ApiGetArg(uint32_t, protocol);
    m3ApiGetArg(uint32_t, code);
    m3ApiGetArg(uint32_t, bits);

    ipp_ir_code_t ir = {
        .protocol = (uint8_t)protocol,
        .code = code,
        .bits = (uint8_t)bits,
    };
    tool_send_display(IPP_MSG_IR_SEND, &ir, sizeof(ir));
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_ir_recv) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (buf_offset >= mem_size || 6 > mem_size - buf_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    if (!ir_pending)
        m3ApiReturn(0);

    ipp_ir_code_t snap;
    memcpy(&snap, (const void *)&ir_pending_code, sizeof(snap));
    ir_pending = false;

    uint8_t *out = (uint8_t *)_mem + buf_offset;
    out[0] = snap.protocol;
    out[1] = (uint8_t)(snap.code);
    out[2] = (uint8_t)(snap.code >> 8);
    out[3] = (uint8_t)(snap.code >> 16);
    out[4] = (uint8_t)(snap.code >> 24);
    out[5] = snap.bits;

    m3ApiReturn(1);
}

/* ──────────────────────────────────────────────────────────
 * External Bus Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_ext_i2c_xfer) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, addr);
    m3ApiGetArg(uint32_t, tx_offset);
    m3ApiGetArg(uint32_t, tx_len);
    m3ApiGetArg(uint32_t, rx_offset);
    m3ApiGetArg(uint32_t, rx_len);

    if (addr < 0x08 || addr > 0x77)
        m3ApiReturn(-1);
    if (tx_len > 256 || rx_len > 256)
        m3ApiReturn(-1);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (tx_len > 0 && (tx_offset >= mem_size || tx_len > mem_size - tx_offset))
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    if (rx_len > 0 && (rx_offset >= mem_size || rx_len > mem_size - rx_offset))
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    int ret;
    if (tx_len > 0) {
        const uint8_t *tx = (const uint8_t *)_mem + tx_offset;
        bool nostop = (rx_len > 0);
        ret = i2c_write_blocking(EXT_I2C, (uint8_t)addr, tx, tx_len, nostop);
        if (ret < 0)
            m3ApiReturn(-1);
    }
    if (rx_len > 0) {
        uint8_t *rx = (uint8_t *)_mem + rx_offset;
        ret = i2c_read_blocking(EXT_I2C, (uint8_t)addr, rx, rx_len, false);
        if (ret < 0)
            m3ApiReturn(-1);
    }
    m3ApiReturn(0);
}

m3ApiRawFunction(m3_app_ext_spi_xfer) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, tx_offset);
    m3ApiGetArg(uint32_t, rx_offset);
    m3ApiGetArg(uint32_t, len);

    if (len > 4096)
        m3ApiReturn(-1);

    uint32_t mem_size = m3_GetMemorySize(runtime);
    if (tx_offset >= mem_size || len > mem_size - tx_offset ||
        rx_offset >= mem_size || len > mem_size - rx_offset)
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);

    const uint8_t *tx = (const uint8_t *)_mem + tx_offset;
    uint8_t *rx = (uint8_t *)_mem + rx_offset;
    int ret = spi_write_read_blocking(EXT_SPI, tx, rx, len);
    m3ApiReturn(ret == (int)len ? 0 : -1);
}

m3ApiRawFunction(m3_app_ext_uart_write) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, data_offset);
    m3ApiGetArg(uint32_t, data_len);
    (void)runtime; (void)data_offset; (void)data_len;
    m3ApiReturn(-1);
}

m3ApiRawFunction(m3_app_ext_uart_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, buf_max);
    (void)runtime; (void)buf_offset; (void)buf_max;
    m3ApiReturn(-1);
}

/* ──────────────────────────────────────────────────────────
 * WiFi/BLE Stubs (Tier 3 — need Bottlenose integration)
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_wifi_scan_start) {
    (void)runtime; (void)_ctx; (void)_sp; (void)_mem;
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_wifi_scan_get) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, buf_max);
    (void)runtime; (void)buf_offset; (void)buf_max;
    m3ApiReturn(-1);
}

m3ApiRawFunction(m3_app_ble_scan_start) {
    (void)runtime; (void)_ctx; (void)_sp; (void)_mem;
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_ble_scan_get) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, buf_max);
    (void)runtime; (void)buf_offset; (void)buf_max;
    m3ApiReturn(-1);
}

/* ──────────────────────────────────────────────────────────
 * Audio Stubs (Tier 3 — need I2S/PDM drivers)
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_audio_tone) {
    m3ApiGetArg(uint32_t, freq_hz);
    m3ApiGetArg(uint32_t, duration_ms);
    (void)freq_hz; (void)duration_ms;
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_mic_read) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(uint32_t, buf_offset);
    m3ApiGetArg(uint32_t, samples);
    (void)runtime; (void)buf_offset; (void)samples;
    m3ApiReturn(-1);
}

/* ──────────────────────────────────────────────────────────
 * System Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_sleep_ms) {
    m3ApiGetArg(uint32_t, ms);
    if (ms > 10000) ms = 10000;

    uint32_t remaining = ms;
    while (remaining > 0) {
        uint32_t chunk = remaining > 50 ? 50 : remaining;
        sleep_ms(chunk);
        remaining -= chunk;
        uint8_t dummy;
        tool_poll_button(&dummy);
        WASM_CHECK_STOP();
    }
    m3ApiSuccess();
}

m3ApiRawFunction(m3_app_exit) {
    (void)runtime; (void)_ctx; (void)_sp; (void)_mem;
    if (g_wasm_ctx)
        g_wasm_ctx->exit_requested = true;
    m3ApiTrap(m3Err_trapExit);
}

m3ApiRawFunction(m3_app_get_battery_pct) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn((uint32_t)battery_pct);
}

m3ApiRawFunction(m3_app_get_ticks) {
    m3ApiReturnType(uint32_t);
    m3ApiReturn((uint32_t)to_ms_since_boot(get_absolute_time()));
}

/* ──────────────────────────────────────────────────────────
 * Link All Host Functions
 * ────────────────────────────────────────────────────────── */

M3Result wasm_link_all(IM3Module module) {
    M3Result r;
    const char *mod = "env";

#define LINK(name, sig) \
    r = m3_LinkRawFunction(module, mod, #name, sig, m3_##name); \
    if (r && r != m3Err_functionLookupFailed) return r;

    // Display — original
    LINK(app_clear_screen,    "v()")
    LINK(app_draw_text,       "v(iiiii)")
    LINK(app_draw_rect,       "v(iiiiii)")
    LINK(app_draw_line,       "v(iiiii)")
    LINK(app_fb_flip,         "v()")
    LINK(app_toast,           "v(iii)")

    // Display — new
    LINK(app_draw_text_small, "v(iiiii)")
    LINK(app_set_pixel,       "v(iii)")
    LINK(app_draw_circle,     "v(iiiii)")
    LINK(app_set_backlight,   "v(i)")
    LINK(app_progress,        "v(iii)")

    // Input
    LINK(app_get_button,      "i()")
    LINK(app_wait_button,     "i()")

    // Radio 1
    LINK(app_radio_set_freq,  "v(i)")
    LINK(app_radio_get_rssi,  "i()")
    LINK(app_radio_tx,        "v(ii)")
    LINK(app_radio_rx,        "i(ii)")
    LINK(app_radio_set_config,"v(iii)")

    // Radio 2
    LINK(app_radio2_set_freq, "v(i)")
    LINK(app_radio2_get_rssi, "i()")
    LINK(app_radio2_tx,       "v(ii)")

    // GPIO
    LINK(app_gpio_read,       "i(i)")
    LINK(app_gpio_write,      "v(ii)")

    // Storage
    LINK(app_fs_write,        "i(iiii)")
    LINK(app_fs_read,         "i(iiii)")
    LINK(app_fs_delete,       "i(ii)")
    LINK(app_fs_list,         "i(ii)")

    // LEDs
    LINK(app_set_led,         "v(iiii)")

    // Sensors
    LINK(app_get_accel,       "i(i)")
    LINK(app_get_time,        "i(i)")
    LINK(app_get_battery_mv,  "i()")
    LINK(app_get_charging,    "i()")
    LINK(app_ioexp_write,     "i(ii)")
    LINK(app_ioexp_read,      "i(i)")

    // IR
    LINK(app_ir_send,         "v(iii)")
    LINK(app_ir_recv,         "i(i)")

    // External buses
    LINK(app_ext_i2c_xfer,    "i(iiiii)")
    LINK(app_ext_spi_xfer,    "i(iii)")
    LINK(app_ext_uart_write,  "i(ii)")
    LINK(app_ext_uart_read,   "i(ii)")

    // WiFi/BLE stubs
    LINK(app_wifi_scan_start, "v()")
    LINK(app_wifi_scan_get,   "i(ii)")
    LINK(app_ble_scan_start,  "v()")
    LINK(app_ble_scan_get,    "i(ii)")

    // Audio stubs
    LINK(app_audio_tone,      "v(ii)")
    LINK(app_mic_read,        "i(ii)")

    // System
    LINK(app_sleep_ms,        "v(i)")
    LINK(app_exit,            "v()")
    LINK(app_get_battery_pct, "i()")
    LINK(app_get_ticks,       "i()")

#undef LINK
    return m3Err_none;
}

/* ──────────────────────────────────────────────────────────
 * Runtime: Load / Run / Stop
 * ────────────────────────────────────────────────────────── */

static uint8_t wasm_file_buf[WASM_MAX_FILE_SIZE];

int wasm_load(wasm_ctx_t *ctx, const char *path) {
    memset(ctx, 0, sizeof(*ctx));

    int rd = fs_read(path, wasm_file_buf, sizeof(wasm_file_buf));
    if (rd < 8) {
        printf("[WASM] read failed: %d\n", rd);
        return -1;
    }

    ctx->wasm_data = wasm_file_buf;
    ctx->wasm_size = (uint32_t)rd;

    ctx->env = m3_NewEnvironment();
    if (!ctx->env) {
        printf("[WASM] env alloc failed\n");
        return -2;
    }

    ctx->runtime = m3_NewRuntime(ctx->env, WASM_STACK_SIZE, NULL);
    if (!ctx->runtime) {
        printf("[WASM] runtime alloc failed\n");
        m3_FreeEnvironment(ctx->env);
        ctx->env = NULL;
        return -3;
    }

    M3Result result = m3_ParseModule(ctx->env, &ctx->module,
                                     ctx->wasm_data, ctx->wasm_size);
    if (result) {
        printf("[WASM] parse: %s\n", result);
        m3_FreeRuntime(ctx->runtime);
        m3_FreeEnvironment(ctx->env);
        ctx->runtime = NULL;
        ctx->env = NULL;
        return -4;
    }

    result = m3_LoadModule(ctx->runtime, ctx->module);
    if (result) {
        printf("[WASM] load: %s\n", result);
        m3_FreeModule(ctx->module);
        m3_FreeRuntime(ctx->runtime);
        m3_FreeEnvironment(ctx->env);
        ctx->module = NULL;
        ctx->runtime = NULL;
        ctx->env = NULL;
        return -5;
    }

    result = wasm_link_all(ctx->module);
    if (result) {
        printf("[WASM] link: %s\n", result);
        wasm_stop(ctx);
        return -6;
    }

    tool_wasm_force_stop = false;

    ctx->running = true;
    ctx->exit_requested = false;
    g_wasm_ctx = ctx;

    printf("[WASM] loaded %u bytes from %s\n", ctx->wasm_size, path);
    return 0;
}

int wasm_run(wasm_ctx_t *ctx) {
    if (!ctx->running || !ctx->runtime) return -1;

    IM3Function start_fn;
    M3Result result = m3_FindFunction(&start_fn, ctx->runtime, "_start");
    if (result) {
        result = m3_FindFunction(&start_fn, ctx->runtime, "main");
        if (result) {
            printf("[WASM] no _start or main: %s\n", result);
            return -1;
        }
    }

    result = m3_CallV(start_fn);
    if (result && result != m3Err_trapExit) {
        M3ErrorInfo info;
        m3_GetErrorInfo(ctx->runtime, &info);
        printf("[WASM] trap: %s", result);
        if (info.message) printf(" (%s)", info.message);
        printf("\n");
        return -2;
    }

    return 0;
}

void wasm_stop(wasm_ctx_t *ctx) {
    if (!ctx) return;

    ctx->running = false;
    ctx->exit_requested = true;
    g_wasm_ctx = NULL;

    if (ctx->runtime) {
        m3_FreeRuntime(ctx->runtime);
        ctx->runtime = NULL;
    }
    ctx->module = NULL;
    if (ctx->env) {
        m3_FreeEnvironment(ctx->env);
        ctx->env = NULL;
    }

    cc1101_idle(PIN_RADIO1_CS);
    cc1101_idle(PIN_RADIO2_CS);
}
