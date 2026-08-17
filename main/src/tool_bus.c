/**
 * tool_bus.c — Bus Analysis Tool
 *
 * Akhlut CFW
 *
 * Modes:
 *   I2C Scan (Header)   — probes external I2C0 bus (header pins)
 *   I2C Scan (Internal)  — probes Display I2C1 bus via IPP
 *   UART Passthrough      — bridges USB CDC ↔ UART1 (Orca header)
 *   SPI Monitor           — placeholder
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio/driver.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>

#define BUS_MODE_MENU     0
#define BUS_MODE_I2C_SCAN 1
#define BUS_MODE_I2C_DONE 2
#define BUS_MODE_INT_WAIT 3
#define BUS_MODE_INT_DONE 4
#define BUS_MODE_PASSTHRU 5

#define MENU_ITEM_COUNT 4

#define MAX_I2C_FOUND 16

static const uint32_t passthru_bauds[] = { 115200, 460800, 921600 };
#define PASSTHRU_BAUD_COUNT \
    (sizeof(passthru_bauds) / sizeof(passthru_bauds[0]))

typedef struct {
    uint8_t  mode;
    uint8_t  menu_sel;
    uint8_t  found_addrs[MAX_I2C_FOUND];
    uint8_t  found_count;
    uint8_t  scan_addr;
    bool     scanning;
    bool     is_internal;
    /* passthrough state */
    uint8_t  baud_idx;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t last_screen_ms;
} bus_state_t;

_Static_assert(sizeof(bus_state_t) <= TOOL_STATE_POOL_SIZE,
               "bus_state_t exceeds state pool");

/* Forward declarations */
static void show_bus_menu(bus_state_t *s);
static void show_i2c_progress(bus_state_t *s);
static void show_i2c_results(bus_state_t *s);
static void show_passthru(bus_state_t *s);
static void passthru_start(bus_state_t *s);
static void passthru_stop(void);

static const char *known_device_name(uint8_t addr) {
    switch (addr) {
    case 0x19: return "LIS3DH";
    case 0x21: return "PCA9555";
    case 0x6B: return "BQ25892";
    case 0x6F: return "MCP7940";
    default:   return NULL;
    }
}

/* ──────────────────────────────────────────────────────────
 * Display
 * ────────────────────────────────────────────────────────── */
static void show_bus_menu(bus_state_t *s) {
    const char *items[] = {
        "I2C Scan (Header)",
        "I2C Scan (Internal)",
        "UART Passthrough",
        "SPI Monitor",
    };
    uint8_t icons[] = { 4, 4, 4, 4 };
    tool_send_menu(s->menu_sel, items, icons, MENU_ITEM_COUNT);
}

static void show_i2c_progress(bus_state_t *s) {
    char lb[8][54];
    const char *lp[8];
    int n = 0;

    snprintf(lb[n], 54, "  I2C BUS SCAN");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    unsigned pct = (unsigned)s->scan_addr * 100 / 127;
    snprintf(lb[n], 54, "  Scanning 0x%02X / 0x7F (%u%%)",
             s->scan_addr, pct);
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  %u device%s found",
             s->found_count,
             s->found_count == 1 ? "" : "s");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;
    lp[n] = "  [RED] Abort"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static void show_i2c_results(bus_state_t *s) {
    char lb[16][54];
    const char *lp[16];
    int n = 0;

    if (s->is_internal)
        snprintf(lb[n], 54, "  I2C INTERNAL RESULTS");
    else
        snprintf(lb[n], 54, "  I2C SCAN RESULTS");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    if (s->found_count == 0) {
        lp[n] = "  No devices found"; n++;
    } else {
        snprintf(lb[n], 54, "  %u device%s found:",
                 s->found_count,
                 s->found_count == 1 ? "" : "s");
        lp[n] = lb[n]; n++;

        lp[n] = ""; n++;

        for (uint8_t i = 0; i < s->found_count && n < 14; i++) {
            const char *name = s->is_internal
                ? known_device_name(s->found_addrs[i])
                : NULL;
            if (name)
                snprintf(lb[n], 54, "  0x%02X  %s",
                         s->found_addrs[i], name);
            else
                snprintf(lb[n], 54, "  0x%02X  (%u)",
                         s->found_addrs[i], s->found_addrs[i]);
            lp[n] = lb[n]; n++;
        }
    }

    lp[n] = ""; n++;
    lp[n] = "  [GREEN] Rescan  [RED] Back"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

static void show_int_waiting(void) {
    const char *lines[] = {
        "  I2C INTERNAL SCAN",
        "",
        "  Scanning Display I2C bus...",
        "",
        "  [RED] Abort",
    };
    tool_send_text_screen(lines, 5);
}

static void show_passthru(bus_state_t *s) {
    char lb[10][54];
    const char *lp[10];
    int n = 0;

    snprintf(lb[n], 54, "  UART PASSTHROUGH");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    snprintf(lb[n], 54, "  Baud: %lu", passthru_bauds[s->baud_idx]);
    lp[n] = lb[n]; n++;

    snprintf(lb[n], 54, "  TX: %lu bytes  RX: %lu bytes",
             s->tx_count, s->rx_count);
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;
    lp[n] = "  COM12 <-> UART1 (Orca)"; n++;
    lp[n] = "  No flow control"; n++;

    lp[n] = ""; n++;
    lp[n] = "  [GRAY] Cycle baud  [RED] Exit"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ──────────────────────────────────────────────────────────
 * Start helpers
 * ────────────────────────────────────────────────────────── */
static void start_ext_scan(bus_state_t *s) {
    s->mode = BUS_MODE_I2C_SCAN;
    s->scanning = true;
    s->scan_addr = 0x00;
    s->found_count = 0;
    s->is_internal = false;
    tool_send_status_bar("I2C Scan");
    show_i2c_progress(s);
}

static void start_int_scan(bus_state_t *s) {
    s->mode = BUS_MODE_INT_WAIT;
    s->found_count = 0;
    s->is_internal = true;
    tool_send_status_bar("I2C Internal");
    show_int_waiting();
    tool_send_display(IPP_MSG_I2C_SCAN_REQ, NULL, 0);
}

static void passthru_start(bus_state_t *s) {
    s->mode = BUS_MODE_PASSTHRU;
    s->tx_count = 0;
    s->rx_count = 0;
    s->last_screen_ms = 0;

    uart_deinit(EXT_UART);
    uart_init(EXT_UART, passthru_bauds[s->baud_idx]);
    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(EXT_UART, false, false);
    uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(EXT_UART, true);

    /* Disable UART1 IRQ in case orca probe had enabled it */
    irq_set_enabled(EXT_UART_IRQ, false);

    tool_serial_passthru = true;

    tool_send_status_bar("UART Passthru");
    show_passthru(s);

    printf("\r\n--- UART Passthrough @ %lu baud ---\r\n",
           passthru_bauds[s->baud_idx]);
}

static void passthru_stop(void) {
    tool_serial_passthru = false;

    uart_deinit(EXT_UART);
    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);

    printf("\r\n--- Passthrough ended ---\r\n> ");
}

/* ──────────────────────────────────────────────────────────
 * Tool Callbacks
 * ────────────────────────────────────────────────────────── */
static tool_result_t bus_enter(tool_ctx_t *ctx) {
    bus_state_t *s = (bus_state_t *)ctx->state;

    s->mode = BUS_MODE_MENU;
    s->menu_sel = 0;
    s->baud_idx = 0;

    tool_send_status_bar("Bus Analysis");
    show_bus_menu(s);

    return TOOL_OK;
}

static tool_result_t bus_update(tool_ctx_t *ctx) {
    bus_state_t *s = (bus_state_t *)ctx->state;

    /* ── External I2C scan (4 addrs per tick) ── */
    if (s->mode == BUS_MODE_I2C_SCAN && s->scanning) {
        for (int batch = 0; batch < 4 && s->scan_addr <= 0x77; batch++) {
            if (s->scan_addr < 0x08) {
                s->scan_addr++;
                continue;
            }

            uint8_t dummy;
            int ret = i2c_read_blocking(EXT_I2C, s->scan_addr,
                                         &dummy, 1, false);
            if (ret >= 0 && s->found_count < MAX_I2C_FOUND) {
                s->found_addrs[s->found_count++] = s->scan_addr;
            }

            s->scan_addr++;
        }

        if (s->scan_addr > 0x77) {
            s->scanning = false;
            s->mode = BUS_MODE_I2C_DONE;
            tool_send_status_bar("I2C Results");
            show_i2c_results(s);
        } else {
            show_i2c_progress(s);
        }
        return TOOL_OK;
    }

    /* ── UART Passthrough ── */
    if (s->mode == BUS_MODE_PASSTHRU) {
        /* USB CDC → UART1 */
        for (int i = 0; i < 64; i++) {
            int ch = getchar_timeout_us(0);
            if (ch == PICO_ERROR_TIMEOUT) break;
            uart_putc_raw(EXT_UART, (char)ch);
            s->tx_count++;
        }

        /* UART1 → USB CDC */
        for (int i = 0; i < 64; i++) {
            if (!uart_is_readable(EXT_UART)) break;
            uint8_t byte = uart_getc(EXT_UART);
            putchar_raw(byte);
            s->rx_count++;
        }

        /* Update screen every 500ms */
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - s->last_screen_ms >= 500) {
            s->last_screen_ms = now;
            show_passthru(s);
        }

        return TOOL_OK;
    }

    return TOOL_OK;
}

static tool_result_t bus_on_button(tool_ctx_t *ctx,
                                    uint8_t btn_id,
                                    uint8_t btn_state) {
    bus_state_t *s = (bus_state_t *)ctx->state;

    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (s->mode) {
    case BUS_MODE_MENU:
        switch (btn_id) {
        case IPP_BTN_YELLOW:
            if (s->menu_sel > 0) s->menu_sel--;
            else s->menu_sel = MENU_ITEM_COUNT - 1;
            show_bus_menu(s);
            break;
        case IPP_BTN_BLUE:
            if (s->menu_sel < MENU_ITEM_COUNT - 1) s->menu_sel++;
            else s->menu_sel = 0;
            show_bus_menu(s);
            break;
        case IPP_BTN_GREEN:
            if (s->menu_sel == 0) {
                start_ext_scan(s);
            } else if (s->menu_sel == 1) {
                start_int_scan(s);
            } else if (s->menu_sel == 2) {
                passthru_start(s);
            } else {
                tool_send_toast("Coming soon", 1000);
            }
            break;
        case IPP_BTN_RED:
            return TOOL_EXIT;
        }
        break;

    case BUS_MODE_I2C_SCAN:
        if (btn_id == IPP_BTN_RED) {
            s->scanning = false;
            s->mode = BUS_MODE_I2C_DONE;
            tool_send_status_bar("I2C Results");
            show_i2c_results(s);
        }
        break;

    case BUS_MODE_INT_WAIT:
        if (btn_id == IPP_BTN_RED) {
            s->mode = BUS_MODE_MENU;
            tool_send_status_bar("Bus Analysis");
            show_bus_menu(s);
        }
        break;

    case BUS_MODE_I2C_DONE:
    case BUS_MODE_INT_DONE:
        switch (btn_id) {
        case IPP_BTN_GREEN:
            if (s->is_internal)
                start_int_scan(s);
            else
                start_ext_scan(s);
            break;
        case IPP_BTN_RED:
            s->mode = BUS_MODE_MENU;
            tool_send_status_bar("Bus Analysis");
            show_bus_menu(s);
            break;
        }
        break;

    case BUS_MODE_PASSTHRU:
        if (btn_id == IPP_BTN_GRAY) {
            s->baud_idx = (s->baud_idx + 1) % PASSTHRU_BAUD_COUNT;
            s->tx_count = 0;
            s->rx_count = 0;

            uart_deinit(EXT_UART);
            uart_init(EXT_UART, passthru_bauds[s->baud_idx]);
            gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
            gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
            uart_set_hw_flow(EXT_UART, false, false);
            uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
            uart_set_fifo_enabled(EXT_UART, true);

            printf("\r\n--- Baud changed to %lu ---\r\n",
                   passthru_bauds[s->baud_idx]);
            show_passthru(s);
        } else if (btn_id == IPP_BTN_RED) {
            passthru_stop();
            s->mode = BUS_MODE_MENU;
            tool_send_status_bar("Bus Analysis");
            show_bus_menu(s);
        }
        break;
    }

    return TOOL_OK;
}

static tool_result_t bus_on_display_msg(tool_ctx_t *ctx, uint8_t type,
                                         const uint8_t *payload, uint16_t len) {
    bus_state_t *s = (bus_state_t *)ctx->state;

    if (type == IPP_MSG_I2C_SCAN_RESP && s->mode == BUS_MODE_INT_WAIT) {
        if (len >= 1) {
            const ipp_i2c_scan_resp_t *resp = (const ipp_i2c_scan_resp_t *)payload;
            s->found_count = resp->count;
            if (s->found_count > MAX_I2C_FOUND)
                s->found_count = MAX_I2C_FOUND;
            for (uint8_t i = 0; i < s->found_count; i++)
                s->found_addrs[i] = resp->addrs[i];

            printf("[BusInt] %u devices found:", s->found_count);
            for (uint8_t i = 0; i < s->found_count; i++)
                printf(" 0x%02X", s->found_addrs[i]);
            printf("\n");
        }
        s->mode = BUS_MODE_INT_DONE;
        tool_send_status_bar("I2C Internal");
        show_i2c_results(s);
    }

    return TOOL_OK;
}

static void bus_exit(tool_ctx_t *ctx) {
    bus_state_t *s = (bus_state_t *)ctx->state;
    if (s->mode == BUS_MODE_PASSTHRU) {
        passthru_stop();
    }
}

const tool_desc_t tool_bus_desc = {
    .name         = "Bus Analysis",
    .icon_id      = 4,
    .requires     = 0,
    .state_size   = sizeof(bus_state_t),
    .enter        = bus_enter,
    .update       = bus_update,
    .on_button    = bus_on_button,
    .exit         = bus_exit,
    .on_orca_msg  = NULL,
    .on_display_msg = bus_on_display_msg,
};
