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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static wasm_ctx_t *g_wasm_ctx = NULL;

/* ──────────────────────────────────────────────────────────
 * Display Host Functions
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
 * Input Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_get_button) {
    m3ApiReturnType(uint32_t);

    extern volatile bool tool_wasm_force_stop;
    uint8_t btn;
    if (tool_poll_button(&btn))
        m3ApiReturn((uint32_t)btn);
    if (tool_wasm_force_stop) {
        if (g_wasm_ctx) g_wasm_ctx->exit_requested = true;
        m3ApiTrap(m3Err_trapExit);
    }
    m3ApiReturn(0xFF);
}

m3ApiRawFunction(m3_app_wait_button) {
    m3ApiReturnType(uint32_t);

    extern volatile bool tool_wasm_force_stop;
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
 * Radio Host Functions
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

    // Only allow writes under /apps/
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
 * System Host Functions
 * ────────────────────────────────────────────────────────── */

m3ApiRawFunction(m3_app_sleep_ms) {
    m3ApiGetArg(uint32_t, ms);
    if (ms > 10000) ms = 10000;

    extern volatile bool tool_wasm_force_stop;
    uint32_t remaining = ms;
    while (remaining > 0) {
        uint32_t chunk = remaining > 50 ? 50 : remaining;
        sleep_ms(chunk);
        remaining -= chunk;
        uint8_t dummy;
        tool_poll_button(&dummy);
        if (tool_wasm_force_stop) {
            if (g_wasm_ctx) g_wasm_ctx->exit_requested = true;
            m3ApiTrap(m3Err_trapExit);
        }
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
    m3ApiReturn(0);
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

    // Display
    LINK(app_clear_screen,   "v()")
    LINK(app_draw_text,      "v(iiiii)")
    LINK(app_draw_rect,      "v(iiiiii)")
    LINK(app_draw_line,      "v(iiiii)")
    LINK(app_fb_flip,        "v()")
    LINK(app_toast,          "v(iii)")

    // Input
    LINK(app_get_button,     "i()")
    LINK(app_wait_button,    "i()")

    // Radio
    LINK(app_radio_set_freq, "v(i)")
    LINK(app_radio_get_rssi, "i()")
    LINK(app_radio_tx,       "v(ii)")

    // GPIO
    LINK(app_gpio_read,      "i(i)")
    LINK(app_gpio_write,     "v(ii)")

    // Storage
    LINK(app_fs_write,       "i(iiii)")
    LINK(app_fs_read,        "i(iiii)")

    // LEDs
    LINK(app_set_led,        "v(iiii)")

    // System
    LINK(app_sleep_ms,       "v(i)")
    LINK(app_exit,           "v()")
    LINK(app_get_battery_pct,"i()")

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

    extern volatile bool tool_wasm_force_stop;
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
}
