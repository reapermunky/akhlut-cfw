/**
 * tool.c — Tool Engine Implementation
 *
 * Akhlut CFW
 *
 * Manages tool registration, lifecycle, state pool, and
 * provides display helpers that tools call from callbacks.
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include <string.h>

/* ──────────────────────────────────────────────────────────
 * Engine State
 * ────────────────────────────────────────────────────────── */
static const tool_desc_t *registry[TOOL_MAX_REGISTERED];
static int                reg_count = 0;

static uint8_t          state_pool[TOOL_STATE_POOL_SIZE];
static tool_ctx_t       active_ctx;
static bool             active = false;

static tool_ipp_send_fn ipp_send = NULL;
static tool_ipp_send_fn orca_send = NULL;
volatile bool tool_serial_passthru = false;
static tool_hw_status_t hw_status;

/* ──────────────────────────────────────────────────────────
 * Engine Lifecycle
 * ────────────────────────────────────────────────────────── */
void tool_engine_init(tool_ipp_send_fn send_fn) {
    ipp_send = send_fn;
    orca_send = NULL;
    reg_count = 0;
    active = false;
    memset(&hw_status, 0, sizeof(hw_status));
    memset(&active_ctx, 0, sizeof(active_ctx));
}

void tool_engine_set_orca_send(tool_ipp_send_fn fn) {
    orca_send = fn;
}

void tool_engine_set_hw(const tool_hw_status_t *hw) {
    hw_status = *hw;
    if (active) {
        active_ctx.hw = *hw;
    }
}

/* ──────────────────────────────────────────────────────────
 * Registration
 * ────────────────────────────────────────────────────────── */
int tool_register(const tool_desc_t *desc) {
    if (reg_count >= TOOL_MAX_REGISTERED) return -1;
    if (desc->state_size > TOOL_STATE_POOL_SIZE) return -1;
    registry[reg_count] = desc;
    return reg_count++;
}

int tool_count(void) {
    return reg_count;
}

const tool_desc_t *tool_get(int index) {
    if (index < 0 || index >= reg_count) return NULL;
    return registry[index];
}

bool tool_hw_available(int index) {
    const tool_desc_t *t = tool_get(index);
    if (!t) return false;

    uint32_t r = t->requires;
    if ((r & REQUIRE_RADIO1) && !hw_status.radio1_ok) return false;
    if ((r & REQUIRE_RADIO2) && !hw_status.radio2_ok) return false;
    if ((r & REQUIRE_RADIO_ANY) &&
        !hw_status.radio1_ok && !hw_status.radio2_ok) return false;
    if ((r & REQUIRE_FPGA) && !hw_status.fpga_ok) return false;
    if ((r & REQUIRE_ORCA) && !hw_status.orca_ok) return false;

    return true;
}

/* ──────────────────────────────────────────────────────────
 * Runtime
 * ────────────────────────────────────────────────────────── */
bool tool_launch(int index) {
    if (active) return false;

    const tool_desc_t *desc = tool_get(index);
    if (!desc) return false;
    if (!desc->enter) return false;

    size_t sz = desc->state_size > 0 ? desc->state_size : 1;
    memset(state_pool, 0, sz);

    active_ctx.desc  = desc;
    active_ctx.state = desc->state_size > 0 ? state_pool : NULL;
    active_ctx.hw    = hw_status;

    tool_result_t r = desc->enter(&active_ctx);
    if (r == TOOL_EXIT) {
        if (desc->exit) desc->exit(&active_ctx);
        memset(&active_ctx, 0, sizeof(active_ctx));
        return false;
    }

    active = true;
    return true;
}

void tool_update(void) {
    if (!active) return;
    if (!active_ctx.desc->update) return;

    tool_result_t r = active_ctx.desc->update(&active_ctx);
    if (r == TOOL_EXIT) {
        tool_exit_active();
    }
}

bool tool_on_button(uint8_t btn_id, uint8_t btn_state) {
    if (!active) return false;
    if (!active_ctx.desc->on_button) return false;

    tool_result_t r = active_ctx.desc->on_button(&active_ctx, btn_id, btn_state);
    if (r == TOOL_EXIT) {
        tool_exit_active();
    }
    return true;
}

void tool_exit_active(void) {
    if (!active) return;
    if (active_ctx.desc->exit) {
        active_ctx.desc->exit(&active_ctx);
    }
    active = false;
    memset(&active_ctx, 0, sizeof(active_ctx));
}

bool tool_is_active(void) {
    return active;
}

const char *tool_active_name(void) {
    return active ? active_ctx.desc->name : NULL;
}

/* ──────────────────────────────────────────────────────────
 * Orca + Display Message Plumbing
 * ────────────────────────────────────────────────────────── */
void tool_send_display(uint8_t type, const void *payload, uint16_t len) {
    if (!ipp_send) return;
    ipp_send(type, payload, len);
}

void tool_on_orca_msg(uint8_t type, const uint8_t *payload, uint16_t len) {
    if (!active) return;
    if (!active_ctx.desc->on_orca_msg) return;

    tool_result_t r = active_ctx.desc->on_orca_msg(&active_ctx, type,
                                                     payload, len);
    if (r == TOOL_EXIT) {
        tool_exit_active();
    }
}

void tool_on_display_msg(uint8_t type, const uint8_t *payload, uint16_t len) {
    if (!active) return;
    if (!active_ctx.desc->on_display_msg) return;

    tool_result_t r = active_ctx.desc->on_display_msg(&active_ctx, type,
                                                       payload, len);
    if (r == TOOL_EXIT) {
        tool_exit_active();
    }
}

void tool_send_orca(uint8_t type, const void *payload, uint16_t len) {
    if (!orca_send) return;
    orca_send(type, payload, len);
}

/* ──────────────────────────────────────────────────────────
 * Display Helpers
 *
 * Tools call these from enter/update/on_button callbacks
 * to send UI commands to the Display processor.
 * ────────────────────────────────────────────────────────── */
void tool_send_menu(uint8_t selected, const char * const *items,
                    const uint8_t *icon_ids, uint8_t count) {
    if (!ipp_send) return;
    uint8_t payload[512];
    size_t pos = 0;

    payload[pos++] = selected;
    payload[pos++] = count;

    for (uint8_t i = 0; i < count; i++) {
        payload[pos++] = icon_ids ? icon_ids[i] : 0;
        payload[pos++] = 0;
        size_t nlen = strlen(items[i]) + 1;
        if (pos + nlen > sizeof(payload)) break;
        memcpy(&payload[pos], items[i], nlen);
        pos += nlen;
    }

    ipp_send(IPP_MSG_MENU_SHOW, payload, (uint16_t)pos);
}

void tool_send_status_bar(const char *name) {
    if (!ipp_send) return;
    uint8_t payload[64];
    ipp_status_bar_t *sb = (ipp_status_bar_t *)payload;

    sb->battery_pct = hw_status.battery_pct;
    sb->flags = 0;
    if (hw_status.battery_charging) sb->flags |= STATUS_FLAG_CHARGING;
    if (hw_status.radio1_ok) sb->flags |= STATUS_FLAG_RADIO1_OK;
    if (hw_status.radio2_ok) sb->flags |= STATUS_FLAG_RADIO2_OK;
    if (hw_status.orca_ok)   sb->flags |= STATUS_FLAG_ORCA_OK;
    sb->rssi_radio1 = 0;
    sb->rssi_radio2 = 0;

    size_t nlen = strlen(name) + 1;
    if (sizeof(ipp_status_bar_t) + nlen > sizeof(payload)) return;
    memcpy(&payload[sizeof(ipp_status_bar_t)], name, nlen);

    ipp_send(IPP_MSG_STATUS_BAR, payload,
             (uint16_t)(sizeof(ipp_status_bar_t) + nlen));
}

void tool_send_toast(const char *text, uint16_t duration_ms) {
    if (!ipp_send) return;
    uint8_t payload[64];
    ipp_toast_header_t *th = (ipp_toast_header_t *)payload;
    th->duration_ms = duration_ms;

    size_t tlen = strlen(text) + 1;
    if (sizeof(ipp_toast_header_t) + tlen > sizeof(payload)) return;
    memcpy(&payload[sizeof(ipp_toast_header_t)], text, tlen);

    ipp_send(IPP_MSG_TOAST, payload,
             (uint16_t)(sizeof(ipp_toast_header_t) + tlen));
}

void tool_send_text_screen(const char * const *lines, uint8_t count) {
    if (!ipp_send) return;
    uint8_t payload[512];
    size_t pos = 0;

    payload[pos++] = count;

    for (uint8_t i = 0; i < count; i++) {
        size_t llen = strlen(lines[i]) + 1;
        if (pos + llen > sizeof(payload)) break;
        memcpy(&payload[pos], lines[i], llen);
        pos += llen;
    }

    ipp_send(IPP_MSG_TEXT_SCREEN, payload, (uint16_t)pos);
}

void tool_send_screen_clear(void) {
    if (!ipp_send) return;
    ipp_send(IPP_MSG_SCREEN_CLEAR, NULL, 0);
}
