/**
 * tool_ir.c — IR Remote Tool
 *
 * Akhlut CFW
 *
 * IR receive and transmit via the Display RP2040's IR LED
 * and receiver (routed through IPP). Captures IR codes and
 * can replay them.
 *
 * RX path: Display IR driver → IPP_MSG_IR_RECEIVED → Main
 *          on_display_frame ISR → ir_pending buffer → main loop
 *          → tool_on_display_msg → ir_on_display_msg callback
 *
 * TX path: IR tool → tool_send_display(IPP_MSG_IR_SEND) →
 *          Display on_main_frame → pend_ir_send → ir_driver_send_nec
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

#define IR_MODE_MENU     0
#define IR_MODE_RECEIVE  1
#define IR_MODE_REPLAY   2

#define MAX_IR_CODES     8

typedef struct {
    uint8_t  protocol;
    uint32_t code;
    uint8_t  bits;
} ir_code_t;

typedef struct {
    uint8_t   mode;
    uint8_t   menu_sel;
    ir_code_t codes[MAX_IR_CODES];
    uint8_t   code_count;
    uint8_t   replay_sel;
} ir_state_t;

_Static_assert(sizeof(ir_state_t) <= TOOL_STATE_POOL_SIZE,
               "ir_state_t exceeds state pool");

static const char *proto_name(uint8_t proto) {
    switch (proto) {
    case IR_PROTO_NEC:     return "NEC";
    case IR_PROTO_SONY:    return "Sony";
    case IR_PROTO_RC5:     return "RC5";
    case IR_PROTO_RC6:     return "RC6";
    case IR_PROTO_SAMSUNG: return "Samsung";
    default:               return "Raw";
    }
}

/* Forward declarations */
static void show_ir_menu(ir_state_t *s);
static void show_receive_screen(ir_state_t *s);
static void show_replay_screen(ir_state_t *s);

/* ──────────────────────────────────────────────────────────
 * Display
 * ────────────────────────────────────────────────────────── */
static void show_ir_menu(ir_state_t *s) {
    const char *items[] = { "Receive", "Replay" };
    uint8_t icons[] = { 6, 6 };
    tool_send_menu(s->menu_sel, items, icons, 2);
}

static void show_receive_screen(ir_state_t *s) {
    char lb[14][54];
    const char *lp[14];
    int n = 0;

    snprintf(lb[n], 54, "  IR RECEIVER");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    if (s->code_count == 0) {
        lp[n] = "  Point remote at device..."; n++;
        lp[n] = "  Waiting for IR signal"; n++;
    } else {
        snprintf(lb[n], 54, "  %u code%s captured:",
                 s->code_count,
                 s->code_count == 1 ? "" : "s");
        lp[n] = lb[n]; n++;

        lp[n] = ""; n++;

        uint8_t start = s->code_count > 6 ? s->code_count - 6 : 0;
        for (uint8_t i = start; i < s->code_count && n < 12; i++) {
            ir_code_t *c = &s->codes[i];
            snprintf(lb[n], 54, "  %s  0x%08lX  %u bits",
                     proto_name(c->protocol),
                     (unsigned long)c->code, c->bits);
            lp[n] = lb[n]; n++;
        }
    }

    lp[n] = ""; n++;
    lp[n] = "  [RED] Back"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static void show_replay_screen(ir_state_t *s) {
    char lb[14][54];
    const char *lp[14];
    int n = 0;

    snprintf(lb[n], 54, "  IR REPLAY");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    if (s->code_count == 0) {
        lp[n] = "  No codes captured yet"; n++;
        lp[n] = "  Use Receive first"; n++;
    } else {
        for (uint8_t i = 0; i < s->code_count && n < 12; i++) {
            ir_code_t *c = &s->codes[i];
            const char *sel = (i == s->replay_sel) ? "> " : "  ";
            snprintf(lb[n], 54, "%s%s 0x%08lX %ub",
                     sel, proto_name(c->protocol),
                     (unsigned long)c->code, c->bits);
            lp[n] = lb[n]; n++;
        }

        lp[n] = ""; n++;
        lp[n] = "  [GREEN] Send"; n++;
    }

    lp[n] = "  [RED] Back"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ──────────────────────────────────────────────────────────
 * Tool Callbacks
 * ────────────────────────────────────────────────────────── */
static tool_result_t ir_enter(tool_ctx_t *ctx) {
    ir_state_t *s = (ir_state_t *)ctx->state;

    s->mode = IR_MODE_MENU;
    s->menu_sel = 0;
    s->code_count = 0;
    s->replay_sel = 0;

    tool_send_status_bar("IR Remote");
    show_ir_menu(s);

    return TOOL_OK;
}

static tool_result_t ir_update(tool_ctx_t *ctx) {
    (void)ctx;
    return TOOL_OK;
}

static tool_result_t ir_on_button(tool_ctx_t *ctx,
                                   uint8_t btn_id,
                                   uint8_t btn_state) {
    ir_state_t *s = (ir_state_t *)ctx->state;

    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (s->mode) {
    case IR_MODE_MENU:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            s->menu_sel = s->menu_sel > 0 ? s->menu_sel - 1 : 1;
            show_ir_menu(s);
            break;
        case IPP_BTN_BLUE:
            s->menu_sel = s->menu_sel < 1 ? s->menu_sel + 1 : 0;
            show_ir_menu(s);
            break;
        case IPP_BTN_GREEN:
            if (s->menu_sel == 0) {
                s->mode = IR_MODE_RECEIVE;
                tool_send_status_bar("IR Receive");
                show_receive_screen(s);
            } else {
                s->mode = IR_MODE_REPLAY;
                s->replay_sel = 0;
                tool_send_status_bar("IR Replay");
                show_replay_screen(s);
            }
            break;
        case IPP_BTN_RED:
            return TOOL_EXIT;
        }
        break;

    case IR_MODE_RECEIVE:
        if (btn_id == IPP_BTN_RED) {
            s->mode = IR_MODE_MENU;
            tool_send_status_bar("IR Remote");
            show_ir_menu(s);
        }
        break;

    case IR_MODE_REPLAY:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->code_count > 0) {
                if (s->replay_sel > 0) s->replay_sel--;
                else s->replay_sel = s->code_count - 1;
                show_replay_screen(s);
            }
            break;
        case IPP_BTN_BLUE:
            if (s->code_count > 0) {
                if (s->replay_sel < s->code_count - 1) s->replay_sel++;
                else s->replay_sel = 0;
                show_replay_screen(s);
            }
            break;
        case IPP_BTN_GREEN:
            if (s->code_count > 0) {
                ir_code_t *c = &s->codes[s->replay_sel];
                ipp_ir_code_t tx;
                tx.protocol = c->protocol;
                tx.code = c->code;
                tx.bits = c->bits;
                tool_send_display(IPP_MSG_IR_SEND, &tx, sizeof(tx));
                tool_send_toast("IR sent", 800);
            }
            break;
        case IPP_BTN_RED:
            s->mode = IR_MODE_MENU;
            tool_send_status_bar("IR Remote");
            show_ir_menu(s);
            break;
        }
        break;
    }

    return TOOL_OK;
}

static tool_result_t ir_on_display_msg(tool_ctx_t *ctx, uint8_t type,
                                        const uint8_t *payload, uint16_t len) {
    ir_state_t *s = (ir_state_t *)ctx->state;

    if (type == IPP_MSG_IR_RECEIVED && len >= sizeof(ipp_ir_code_t)) {
        const ipp_ir_code_t *ir = (const ipp_ir_code_t *)payload;

        if (s->code_count < MAX_IR_CODES) {
            ir_code_t *c = &s->codes[s->code_count++];
            c->protocol = ir->protocol;
            c->code     = ir->code;
            c->bits     = ir->bits;

            printf("[IR] Received: proto=%u code=0x%08lX bits=%u\n",
                   ir->protocol, (unsigned long)ir->code, ir->bits);

            if (s->mode == IR_MODE_RECEIVE) {
                show_receive_screen(s);
            }
        }
    }

    return TOOL_OK;
}

static void ir_exit(tool_ctx_t *ctx) {
    (void)ctx;
}

const tool_desc_t tool_ir_desc = {
    .name           = "IR Remote",
    .icon_id        = 6,
    .requires       = 0,
    .state_size     = sizeof(ir_state_t),
    .enter          = ir_enter,
    .update         = ir_update,
    .on_button      = ir_on_button,
    .exit           = ir_exit,
    .on_orca_msg    = NULL,
    .on_display_msg = ir_on_display_msg,
};
