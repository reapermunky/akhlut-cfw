/**
 * tool_about.c — About / System Info Tool
 *
 * Akhlut CFW
 *
 * Displays firmware version, hardware status, and
 * system information. Read-only — no hardware required.
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t dummy;
} about_state_t;

_Static_assert(sizeof(about_state_t) <= TOOL_STATE_POOL_SIZE,
               "about_state_t exceeds state pool");

static void show_about(tool_ctx_t *ctx) {
    char lb[16][54];
    const char *lp[16];
    int n = 0;

    snprintf(lb[n], 54, "  AKHLUT CFW");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    snprintf(lb[n], 54, "  Firmware: v0.1.0");
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Build:    %s", __DATE__);
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Target:   FreeWili 1 OG");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    snprintf(lb[n], 54, "  HARDWARE STATUS");
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Radio 1:  %s",
             ctx->hw.radio1_ok ? "OK" : "N/A");
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Radio 2:  %s",
             ctx->hw.radio2_ok ? "OK" : "N/A");
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  FPGA:     %s",
             ctx->hw.fpga_ok ? "OK" : "N/A");
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  Orca:     %s",
             ctx->hw.orca_ok ? "OK" : "N/A");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    snprintf(lb[n], 54, "  ID: %02X%02X%02X%02X%02X%02X%02X%02X",
             uid.id[0], uid.id[1], uid.id[2], uid.id[3],
             uid.id[4], uid.id[5], uid.id[6], uid.id[7]);
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;
    lp[n] = "  [RED] Back"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static tool_result_t about_enter(tool_ctx_t *ctx) {
    tool_send_status_bar("About");
    show_about(ctx);
    return TOOL_OK;
}

static tool_result_t about_update(tool_ctx_t *ctx) {
    (void)ctx;
    return TOOL_OK;
}

static tool_result_t about_on_button(tool_ctx_t *ctx,
                                      uint8_t btn_id,
                                      uint8_t btn_state) {
    (void)ctx;
    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;
    if (btn_id == IPP_BTN_RED) return TOOL_EXIT;
    return TOOL_OK;
}

static void about_exit(tool_ctx_t *ctx) {
    (void)ctx;
}

const tool_desc_t tool_about_desc = {
    .name         = "About",
    .icon_id      = 8,
    .requires     = 0,
    .state_size   = sizeof(about_state_t),
    .enter        = about_enter,
    .update       = about_update,
    .on_button    = about_on_button,
    .exit         = about_exit,
    .on_orca_msg  = NULL,
};
