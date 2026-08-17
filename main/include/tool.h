/**
 * tool.h — Tool Engine
 *
 * Akhlut CFW
 *
 * Registration, lifecycle, and static state pool for tools.
 * Only one tool runs at a time. The state pool is a flat
 * buffer reused between tool activations — no malloc.
 *
 * Lifecycle: register → launch → enter() → [update() + on_button() loop] → exit()
 */

#ifndef TOOL_H
#define TOOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TOOL_MAX_REGISTERED  16
#define TOOL_STATE_POOL_SIZE 2048

typedef enum {
    TOOL_OK,
    TOOL_EXIT,
} tool_result_t;

typedef struct {
    bool    radio1_ok;
    bool    radio2_ok;
    bool    fpga_ok;
    bool    orca_ok;
    uint8_t battery_pct;
    bool    battery_charging;
} tool_hw_status_t;

typedef struct tool_ctx tool_ctx_t;

typedef struct {
    const char *name;
    uint8_t     icon_id;
    uint32_t    requires;
    size_t      state_size;

    tool_result_t (*enter)(tool_ctx_t *ctx);
    tool_result_t (*update)(tool_ctx_t *ctx);
    tool_result_t (*on_button)(tool_ctx_t *ctx, uint8_t btn_id, uint8_t btn_state);
    void          (*exit)(tool_ctx_t *ctx);
    tool_result_t (*on_orca_msg)(tool_ctx_t *ctx, uint8_t type,
                                  const uint8_t *payload, uint16_t len);
    tool_result_t (*on_display_msg)(tool_ctx_t *ctx, uint8_t type,
                                    const uint8_t *payload, uint16_t len);
} tool_desc_t;

struct tool_ctx {
    const tool_desc_t  *desc;
    void               *state;
    tool_hw_status_t    hw;
};

typedef void (*tool_ipp_send_fn)(uint8_t type, const void *payload, uint16_t len);

/* Engine lifecycle */
void tool_engine_init(tool_ipp_send_fn send_fn);
void tool_engine_set_hw(const tool_hw_status_t *hw);
void tool_engine_set_orca_send(tool_ipp_send_fn fn);

/* Registration */
int  tool_register(const tool_desc_t *desc);
int  tool_count(void);
const tool_desc_t *tool_get(int index);
bool tool_hw_available(int index);

/* Runtime */
bool tool_launch(int index);
void tool_update(void);
bool tool_on_button(uint8_t btn_id, uint8_t btn_state);
void tool_exit_active(void);
bool tool_is_active(void);
const char *tool_active_name(void);

/* Message forwarding */
void tool_on_orca_msg(uint8_t type, const uint8_t *payload, uint16_t len);
void tool_on_display_msg(uint8_t type, const uint8_t *payload, uint16_t len);
void tool_send_orca(uint8_t type, const void *payload, uint16_t len);

/* Poll button events directly from ISR ring buffer (used by WASM runtime) */
bool tool_poll_button(uint8_t *out_id);

/* Flush all pending button events from the ISR ring buffer */
void tool_flush_buttons(void);

/* Serial passthrough — when true, main loop must not consume USB CDC input */
extern volatile bool tool_serial_passthru;

/* Raw UART mode — when true, UART1 ISR fills raw buffer instead of IPP parser */
extern volatile bool tool_orca_raw_mode;

/* Raw Orca text I/O (for GhostESP CLI) */
void     tool_orca_raw_send(const char *text);
uint16_t tool_orca_raw_read(uint8_t *buf, uint16_t max);
uint16_t tool_orca_raw_available(void);
void     tool_orca_raw_flush(void);

/* Generic send — tools call these for arbitrary IPP messages */
void tool_send_display(uint8_t type, const void *payload, uint16_t len);

/* Display helpers — tools call these from callbacks */
void tool_send_menu(uint8_t selected, const char * const *items,
                    const uint8_t *icon_ids, uint8_t count);
void tool_send_status_bar(const char *name);
void tool_send_toast(const char *text, uint16_t duration_ms);
void tool_send_text_screen(const char * const *lines, uint8_t count);
void tool_send_screen_clear(void);

#endif
