/**
 * tool_apps.c — WASM App Loader Tool
 *
 * Akhlut CFW
 *
 * Scans /apps/ on the flash filesystem for .wasm files,
 * displays them in a scrollable menu, and runs selected
 * apps in the wasm3 interpreter.
 *
 * App runs on Core 0 in the tool engine's update loop.
 * RED held for 1s force-stops a running app.
 */

#include "tool.h"
#include "board.h"
#include "fs.h"
#include "wasm_api.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define APPS_MODE_BROWSER  0
#define APPS_MODE_RUNNING  1
#define APPS_MODE_ERROR    2

#define MAX_APP_FILES    16
#define MAX_APP_NAME_LEN 28

typedef struct {
    uint8_t   mode;
    uint8_t   menu_sel;
    uint8_t   menu_scroll;
    uint8_t   app_count;
    char      app_names[MAX_APP_FILES][MAX_APP_NAME_LEN];
    wasm_ctx_t wasm;
} apps_state_t;

_Static_assert(sizeof(apps_state_t) <= TOOL_STATE_POOL_SIZE,
               "apps_state_t exceeds state pool");

/* ──────────────────────────────────────────────────────────
 * File Scanner
 * ────────────────────────────────────────────────────────── */

typedef struct {
    apps_state_t *state;
} scan_ctx_t;

static void scan_cb(const char *name, uint32_t size, void *user) {
    (void)size;
    scan_ctx_t *sc = (scan_ctx_t *)user;
    apps_state_t *s = sc->state;

    size_t nlen = strlen(name);
    if (nlen < 6) return;
    if (strcmp(name + nlen - 5, ".wasm") != 0) return;

    if (s->app_count < MAX_APP_FILES) {
        strncpy(s->app_names[s->app_count], name, MAX_APP_NAME_LEN - 1);
        s->app_names[s->app_count][MAX_APP_NAME_LEN - 1] = '\0';
        s->app_count++;
    }
}

static void scan_apps(apps_state_t *s) {
    s->app_count = 0;
    scan_ctx_t sc = { .state = s };
    fs_list("/apps", scan_cb, &sc);
    s->menu_sel = 0;
    s->menu_scroll = 0;
}

/* ──────────────────────────────────────────────────────────
 * Display: App Browser
 * ────────────────────────────────────────────────────────── */

static void show_browser(apps_state_t *s) {
    char lb[14][54];
    const char *lp[14];
    int n = 0;

    lp[n] = "  WASM APPS"; n++;
    lp[n] = ""; n++;

    if (s->app_count == 0) {
        lp[n] = "  No .wasm files found"; n++;
        lp[n] = "  Place files in /apps/"; n++;
    } else {
        snprintf(lb[n], 54, "  %u app%s:", s->app_count,
                 s->app_count == 1 ? "" : "s");
        lp[n] = lb[n]; n++;
        lp[n] = ""; n++;

        uint8_t start = s->menu_scroll;
        uint8_t visible = 7;
        if (start + visible > s->app_count)
            visible = s->app_count - start;

        for (uint8_t i = 0; i < visible; i++) {
            uint8_t idx = start + i;
            char marker = (idx == s->menu_sel) ? '>' : ' ';
            // Strip .wasm extension for display
            char display[24];
            strncpy(display, s->app_names[idx], 23);
            display[23] = '\0';
            size_t dl = strlen(display);
            if (dl > 5 && strcmp(display + dl - 5, ".wasm") == 0)
                display[dl - 5] = '\0';

            snprintf(lb[n], 54, " %c %s", marker, display);
            lp[n] = lb[n]; n++;
        }
    }

    lp[n] = ""; n++;
    lp[n] = s->app_count > 0
        ? "  [GREEN] Run  [GRAY] Refresh  [RED] Back"
        : "  [GRAY] Refresh  [RED] Back";
    n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static void show_error(const char *msg) {
    const char *lines[6];
    char lb[2][54];

    lines[0] = "  APP ERROR";
    lines[1] = "";
    snprintf(lb[0], 54, "  %s", msg);
    lines[2] = lb[0];
    lines[3] = "";
    lines[4] = "  [RED] Back";

    tool_send_text_screen(lines, 5);
}

/* ──────────────────────────────────────────────────────────
 * Tool Callbacks
 * ────────────────────────────────────────────────────────── */

static tool_result_t apps_enter(tool_ctx_t *ctx) {
    apps_state_t *s = (apps_state_t *)ctx->state;

    s->mode = APPS_MODE_BROWSER;
    scan_apps(s);

    tool_send_status_bar("Apps");
    show_browser(s);
    return TOOL_OK;
}

static tool_result_t apps_update(tool_ctx_t *ctx) {
    (void)ctx;
    return TOOL_OK;
}

static tool_result_t apps_on_button(tool_ctx_t *ctx,
                                     uint8_t btn_id,
                                     uint8_t btn_state) {
    apps_state_t *s = (apps_state_t *)ctx->state;

    if (s->mode == APPS_MODE_ERROR) {
        if (btn_state == BTN_STATE_PRESSED && btn_id == IPP_BTN_RED) {
            s->mode = APPS_MODE_BROWSER;
            tool_send_status_bar("Apps");
            show_browser(s);
        }
        return TOOL_OK;
    }

    // Browser mode
    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (btn_id) {
    case IPP_BTN_YELLOW:
        if (s->app_count > 0 && s->menu_sel > 0) {
            s->menu_sel--;
            if (s->menu_sel < s->menu_scroll)
                s->menu_scroll = s->menu_sel;
            show_browser(s);
        }
        break;

    case IPP_BTN_BLUE:
        if (s->app_count > 0 && s->menu_sel < s->app_count - 1) {
            s->menu_sel++;
            if (s->menu_sel >= s->menu_scroll + 7)
                s->menu_scroll = s->menu_sel - 6;
            show_browser(s);
        }
        break;

    case IPP_BTN_GREEN:
        if (s->app_count > 0) {
            char path[64];
            snprintf(path, sizeof(path), "/apps/%s",
                     s->app_names[s->menu_sel]);

            tool_send_toast("Loading...", 1000);

            int err = wasm_load(&s->wasm, path);
            if (err != 0) {
                printf("[APPS] load failed: %d\n", err);
                s->mode = APPS_MODE_ERROR;
                tool_send_status_bar("Error");
                show_error("Failed to load app");
                break;
            }

            // Strip .wasm for status bar
            char name[24];
            strncpy(name, s->app_names[s->menu_sel], 23);
            name[23] = '\0';
            size_t nl = strlen(name);
            if (nl > 5 && strcmp(name + nl - 5, ".wasm") == 0)
                name[nl - 5] = '\0';

            s->mode = APPS_MODE_RUNNING;
            tool_send_status_bar(name);
            tool_send_screen_clear();

            tool_flush_buttons();

            int result = wasm_run(&s->wasm);

            extern uint32_t last_activity_ms;
            last_activity_ms = to_ms_since_boot(get_absolute_time());

            extern volatile bool tool_wasm_force_stop;
            printf("[APPS] run done: result=%d exit=%d force=%d\n",
                   result, s->wasm.exit_requested, tool_wasm_force_stop);

            if (result != 0 && !s->wasm.exit_requested) {
                wasm_stop(&s->wasm);
                s->mode = APPS_MODE_ERROR;
                tool_send_screen_clear();
                tool_send_status_bar("Error");
                show_error("App crashed");
            } else {
                bool was_forced = tool_wasm_force_stop;
                wasm_stop(&s->wasm);
                s->mode = APPS_MODE_BROWSER;
                tool_send_screen_clear();
                tool_send_status_bar("Apps");
                if (was_forced) tool_send_toast("App stopped", 1500);
                scan_apps(s);
                show_browser(s);
            }
        }
        break;

    case IPP_BTN_GRAY:
        scan_apps(s);
        show_browser(s);
        tool_send_toast("Refreshed", 1000);
        break;

    case IPP_BTN_RED:
        return TOOL_EXIT;
    }

    return TOOL_OK;
}

static void apps_exit(tool_ctx_t *ctx) {
    apps_state_t *s = (apps_state_t *)ctx->state;
    if (s->wasm.running)
        wasm_stop(&s->wasm);
}

/* ──────────────────────────────────────────────────────────
 * Tool Descriptor
 * ────────────────────────────────────────────────────────── */
const tool_desc_t tool_apps_desc = {
    .name         = "Apps",
    .icon_id      = 12,
    .requires     = 0,
    .state_size   = sizeof(apps_state_t),
    .enter        = apps_enter,
    .update       = apps_update,
    .on_button    = apps_on_button,
    .exit         = apps_exit,
    .on_orca_msg  = NULL,
};
