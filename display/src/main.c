/**
 * main.c — FreeWili 1 Display RP2040 Firmware
 *
 * Akhlut CFW
 *
 * Core 0: Buttons, sensors, IPP protocol, event dispatch
 * Core 1: TFT SPI writes (DMA-driven framebuffer flush)
 *
 * This processor is the "face" of the device.
 * It renders what Main tells it to render, and sends
 * input events back to Main. It never makes tool decisions.
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"

#include "pico/bootrom.h"
#include "board_display.h"
#include "ipp_defs.h"
#include "ipp.h"
#include "gfx.h"
#include "ui.h"
#include "ir_driver.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "ws2812.pio.h"

/* ──────────────────────────────────────────────────────────
 * Forward Declarations
 * ────────────────────────────────────────────────────────── */
static void init_buttons(void);
static void init_leds(void);
static void init_i2c_sensors(void);
static void init_ipp_uart(void);
static void scan_buttons(void);
static void send_button_event(uint8_t id, uint8_t state, uint32_t hold_ms);
static void send_render_ready(void);
static void ws2812_show(void);
static void show_splash(void);
static void core1_display_entry(void);
static void process_pending_messages(void);

static void __not_in_flash_func(uart0_irq_handler)(void);
static void __not_in_flash_func(on_main_frame)(const ipp_frame_t *frame, void *user_data);

/* ──────────────────────────────────────────────────────────
 * Global State
 * ────────────────────────────────────────────────────────── */

// IPP
static ipp_rx_ctx_t     ipp_main_rx;
static uint8_t          ipp_tx_buf[IPP_MAX_FRAME_SIZE];
static volatile uint8_t ipp_seq = 0;

// UI widgets
static ui_menu_t        ui_menu;
static ui_status_bar_t  ui_sb;
static ui_hint_bar_t    ui_hb;
static ui_toast_t       ui_toast;
static ui_text_screen_t ui_ts;

// Pending IPP messages (ISR writes payload, main loop parses and renders)
#define PENDING_PAYLOAD_MAX 512

static volatile bool     pend_menu;
static uint8_t           pend_menu_buf[PENDING_PAYLOAD_MAX];
static volatile uint16_t pend_menu_len;

static volatile bool     pend_sb;
static uint8_t           pend_sb_buf[64];
static volatile uint16_t pend_sb_len;

static volatile bool     pend_toast;
static uint8_t           pend_toast_buf[64];
static volatile uint16_t pend_toast_len;

static volatile bool     pend_ts;
static uint8_t           pend_ts_buf[PENDING_PAYLOAD_MAX];
static volatile uint16_t pend_ts_len;

static volatile bool     pend_clear;

// Draw command ring buffer (ISR producer, main loop consumer)
#define DRAW_Q_DEPTH 32
#define DRAW_Q_PAYLOAD 64

typedef struct {
    uint8_t type;
    uint8_t len;
    uint8_t payload[DRAW_Q_PAYLOAD];
} draw_q_entry_t;

static draw_q_entry_t          draw_q[DRAW_Q_DEPTH];
static volatile uint8_t        draw_q_head = 0;
static volatile uint8_t        draw_q_tail = 0;

static volatile bool     pend_ir_send;
static ipp_ir_code_t     pend_ir_code;

static volatile bool     pend_reboot_bootloader;
static volatile bool     pend_i2c_scan;

static volatile bool     pend_ioexp_write;
static volatile uint8_t  pend_ioexp_reg;
static volatile uint8_t  pend_ioexp_val;
static volatile bool     pend_ioexp_read;
static volatile uint8_t  pend_ioexp_read_reg;

// Buttons
typedef struct {
    uint8_t  gpio;
    uint8_t  id;
    bool     pressed;
    uint32_t press_time_ms;
    bool     held_sent;
    uint32_t last_change_ms;
} button_state_t;

static button_state_t buttons[BTN_COUNT] = {
    { PIN_BTN_GRAY,   BTN_ID_GRAY,   false, 0, false },
    { PIN_BTN_YELLOW, BTN_ID_YELLOW, false, 0, false },
    { PIN_BTN_GREEN,  BTN_ID_GREEN,  false, 0, false },
    { PIN_BTN_BLUE,   BTN_ID_BLUE,   false, 0, false },
    { PIN_BTN_RED,    BTN_ID_RED,    false, 0, false },
};

// Current content mode — tracks what's drawn in the content area
#define DISPLAY_MODE_MENU  0
#define DISPLAY_MODE_TEXT  1
static uint8_t display_mode = DISPLAY_MODE_MENU;

static PIO led_pio;
static uint led_sm;
static bool led_pio_inited = false;
static uint32_t led_grb[7];
static volatile bool leds_enabled = false;
static volatile bool pend_led_update = false;

// Sensor presence
static bool accel_present = false;
static bool ioexp_present = false;
static bool charger_present = false;
static bool rtc_present = false;

/* ──────────────────────────────────────────────────────────
 * IPP Transmit Helper
 * ────────────────────────────────────────────────────────── */
static void ipp_send_main(uint8_t type, const void *payload, uint16_t len) {
    size_t frame_len = ipp_build(ipp_seq++, type, payload, len,
                                 ipp_tx_buf, sizeof(ipp_tx_buf));
    if (frame_len > 0) {
        uart_write_blocking(IPP_UART, ipp_tx_buf, frame_len);
    }
}

/* ──────────────────────────────────────────────────────────
 * UART0 ISR — Main Link (MUST run from SRAM)
 * ────────────────────────────────────────────────────────── */
static void __not_in_flash_func(uart0_irq_handler)(void) {
    while (uart_is_readable(IPP_UART)) {
        uint8_t byte = uart_getc(IPP_UART);
        ipp_rx_feed(&ipp_main_rx, byte);
    }
}

/* ──────────────────────────────────────────────────────────
 * IPP Receive Callback — Commands from Main (ISR context)
 *
 * Copies payloads to pending buffers for main-loop processing.
 * No SPI/rendering here — that would block the ISR too long.
 * ────────────────────────────────────────────────────────── */
static void __not_in_flash_func(on_main_frame)(const ipp_frame_t *frame, void *user_data) {
    switch (frame->type) {

    case IPP_MSG_MENU_SHOW:
        if (frame->length <= PENDING_PAYLOAD_MAX) {
            memcpy(pend_menu_buf, frame->payload, frame->length);
            pend_menu_len = frame->length;
            pend_menu = true;
        }
        break;

    case IPP_MSG_STATUS_BAR:
        if (frame->length <= sizeof(pend_sb_buf)) {
            memcpy(pend_sb_buf, frame->payload, frame->length);
            pend_sb_len = frame->length;
            pend_sb = true;
        }
        break;

    case IPP_MSG_TOAST:
        if (frame->length <= sizeof(pend_toast_buf)) {
            memcpy(pend_toast_buf, frame->payload, frame->length);
            pend_toast_len = frame->length;
            pend_toast = true;
        }
        break;

    case IPP_MSG_TEXT_SCREEN:
        if (frame->length <= PENDING_PAYLOAD_MAX) {
            memcpy(pend_ts_buf, frame->payload, frame->length);
            pend_ts_len = frame->length;
            pend_ts = true;
        }
        break;

    case IPP_MSG_SCREEN_CLEAR:
        pend_clear = true;
        draw_q_head = draw_q_tail;
        break;

    case IPP_MSG_BACKLIGHT:
        if (frame->length >= sizeof(ipp_backlight_t)) {
            const ipp_backlight_t *bl = (const ipp_backlight_t *)frame->payload;
            uint slice = pwm_gpio_to_slice_num(PIN_TFT_BACKLIGHT);
            pwm_set_chan_level(slice, PWM_CHAN_B, bl->brightness);
        }
        break;

    case IPP_MSG_IR_SEND:
        if (frame->length >= sizeof(ipp_ir_code_t)) {
            memcpy((void *)&pend_ir_code, frame->payload, sizeof(ipp_ir_code_t));
            pend_ir_send = true;
        }
        break;

    case IPP_MSG_REBOOT_BOOTLOADER:
        pend_reboot_bootloader = true;
        break;

    case IPP_MSG_I2C_SCAN_REQ:
        pend_i2c_scan = true;
        break;

    case IPP_MSG_IOEXP_WRITE:
        if (frame->length >= 2) {
            pend_ioexp_reg = frame->payload[0];
            pend_ioexp_val = frame->payload[1];
            pend_ioexp_write = true;
        }
        break;

    case IPP_MSG_IOEXP_READ:
        if (frame->length >= 1) {
            pend_ioexp_read_reg = frame->payload[0];
            pend_ioexp_read = true;
        }
        break;

    case IPP_MSG_LED_SET:
        if (frame->length >= sizeof(ipp_led_set_t)) {
            const ipp_led_set_t *ls = (const ipp_led_set_t *)frame->payload;
            if (ls->led_index < 7) {
                led_grb[ls->led_index] = ((uint32_t)ls->g << 16) |
                                         ((uint32_t)ls->r << 8) |
                                         (uint32_t)ls->b;
                pend_led_update = true;
            }
        }
        break;

    case IPP_MSG_LED_PATTERN:
        break;

    case IPP_MSG_DRAW_RECT:
    case IPP_MSG_DRAW_LINE:
    case IPP_MSG_DRAW_TEXT:
    case IPP_MSG_FB_FLIP: {
        uint8_t next = (draw_q_head + 1) % DRAW_Q_DEPTH;
        if (next != draw_q_tail) {
            draw_q[draw_q_head].type = frame->type;
            uint8_t copy = frame->length < DRAW_Q_PAYLOAD
                         ? frame->length : DRAW_Q_PAYLOAD;
            memcpy(draw_q[draw_q_head].payload, frame->payload, copy);
            if (copy < DRAW_Q_PAYLOAD)
                draw_q[draw_q_head].payload[copy] = '\0';
            else
                draw_q[draw_q_head].payload[DRAW_Q_PAYLOAD - 1] = '\0';
            draw_q[draw_q_head].len = copy;
            draw_q_head = next;
        }
        break;
    }
    case IPP_MSG_DRAW_CIRCLE:
    case IPP_MSG_DRAW_BATCH:
        break;

    default:
        break;
    }
}

/* ──────────────────────────────────────────────────────────
 * Process Pending IPP Messages (main loop context)
 *
 * Parses buffered payloads into UI widgets and renders them.
 * ────────────────────────────────────────────────────────── */
static void process_pending_messages(void) {
    if (pend_clear) {
        pend_clear = false;
        gfx_clear(COL_BG);
    }

    if (pend_sb) {
        pend_sb = false;

        if (pend_sb_len >= sizeof(ipp_status_bar_t)) {
            const ipp_status_bar_t *sb = (const ipp_status_bar_t *)pend_sb_buf;
            const char *name = "Home";
            if (pend_sb_len > sizeof(ipp_status_bar_t)) {
                name = (const char *)&pend_sb_buf[sizeof(ipp_status_bar_t)];
            }
            strncpy(ui_sb.tool_name, name, sizeof(ui_sb.tool_name) - 1);
            ui_sb.tool_name[sizeof(ui_sb.tool_name) - 1] = '\0';
            ui_sb.battery_pct = sb->battery_pct;
            ui_sb.charging = (sb->flags & STATUS_FLAG_CHARGING) != 0;
            ui_sb.radio1_ok = (sb->flags & STATUS_FLAG_RADIO1_OK) != 0;
            ui_sb.radio2_ok = (sb->flags & STATUS_FLAG_RADIO2_OK) != 0;
            ui_sb.orca_ok = (sb->flags & STATUS_FLAG_ORCA_OK) != 0;
            ui_sb.usb_connected = (sb->flags & STATUS_FLAG_USB) != 0;
            ui_sb.dirty = true;
            ui_status_bar_draw(&ui_sb);
        }
    }

    if (pend_menu) {
        pend_menu = false;

        const uint8_t *p = pend_menu_buf;
        uint16_t len = pend_menu_len;

        if (len >= 2) {
        uint8_t selected = p[0];
        uint8_t count = p[1];
        size_t pos = 2;

        ui_menu_clear(&ui_menu);

        for (uint8_t i = 0; i < count && pos < len; i++) {
            if (pos + 2 > len) break;
            uint8_t icon_id = p[pos++];
            uint8_t flags = p[pos++];

            const char *name = (const char *)&p[pos];
            size_t nlen = strnlen(name, len - pos);
            if (pos + nlen + 1 > len) break;
            pos += nlen + 1;

            ui_menu_add(&ui_menu, name, icon_id,
                        (flags & 0x01) != 0,
                        (flags & 0x02) != 0);
        }

        ui_menu.selected = selected;
        if (selected >= MENU_VISIBLE_ITEMS) {
            ui_menu.scroll_offset = selected - MENU_VISIBLE_ITEMS + 1;
        }
        ui_menu.dirty = true;
        ui_menu_draw(&ui_menu);

        ui_hb.show_nav = true;
        ui_hb.show_select = true;
        ui_hb.show_back = true;
        ui_hb.dirty = true;
        ui_hint_bar_draw(&ui_hb);

        display_mode = DISPLAY_MODE_MENU;

        if (ui_toast.active) {
            ui_toast_draw(&ui_toast, to_ms_since_boot(get_absolute_time()));
        }
        } // len >= 2
    }

    if (pend_ts) {
        pend_ts = false;

        if (pend_ts_len >= 1) {
            uint8_t count = pend_ts_buf[0];
            size_t pos = 1;

            ui_text_clear(&ui_ts);

            for (uint8_t i = 0; i < count && pos < pend_ts_len; i++) {
                const char *line = (const char *)&pend_ts_buf[pos];
                size_t llen = strnlen(line, pend_ts_len - pos);
                if (pos + llen + 1 > pend_ts_len) break;
                pos += llen + 1;
                ui_text_add_line(&ui_ts, line);
            }

            ui_ts.dirty = true;
            ui_text_draw(&ui_ts);

            ui_hb.show_nav = false;
            ui_hb.show_select = false;
            ui_hb.show_back = false;
            ui_hb.gray_action[0] = '\0';
            ui_hb.dirty = true;
            ui_hint_bar_draw(&ui_hb);

            display_mode = DISPLAY_MODE_TEXT;

            if (ui_toast.active) {
                ui_toast_draw(&ui_toast,
                              to_ms_since_boot(get_absolute_time()));
            }
        }
    }

    if (pend_toast) {
        pend_toast = false;

        if (pend_toast_len > sizeof(ipp_toast_header_t)) {
            const ipp_toast_header_t *th =
                (const ipp_toast_header_t *)pend_toast_buf;
            const char *text =
                (const char *)&pend_toast_buf[sizeof(ipp_toast_header_t)];
            ui_toast_show(&ui_toast, text, th->duration_ms);
            ui_toast_draw(&ui_toast, to_ms_since_boot(get_absolute_time()));
        }
    }

    // Drain draw command queue
    while (draw_q_tail != draw_q_head) {
        draw_q_entry_t *e = &draw_q[draw_q_tail];
        draw_q_tail = (draw_q_tail + 1) % DRAW_Q_DEPTH;

        switch (e->type) {
        case IPP_MSG_DRAW_RECT:
            if (e->len >= sizeof(ipp_draw_rect_t)) {
                const ipp_draw_rect_t *r =
                    (const ipp_draw_rect_t *)e->payload;
                if (r->filled)
                    gfx_fill_rect(r->x, r->y, r->w, r->h, r->color);
                else
                    gfx_draw_rect(r->x, r->y, r->w, r->h, r->color);
            }
            break;

        case IPP_MSG_DRAW_LINE:
            if (e->len >= sizeof(ipp_draw_line_t)) {
                const ipp_draw_line_t *l =
                    (const ipp_draw_line_t *)e->payload;
                if (l->y0 == l->y1)
                    gfx_hline(l->x0 < l->x1 ? l->x0 : l->x1, l->y0,
                              l->x0 < l->x1 ? l->x1 - l->x0 + 1
                                             : l->x0 - l->x1 + 1,
                              l->color);
                else if (l->x0 == l->x1)
                    gfx_vline(l->x0, l->y0 < l->y1 ? l->y0 : l->y1,
                              l->y0 < l->y1 ? l->y1 - l->y0 + 1
                                             : l->y0 - l->y1 + 1,
                              l->color);
                else {
                    // Bresenham
                    int16_t dx = l->x1 > l->x0 ? l->x1 - l->x0 : l->x0 - l->x1;
                    int16_t dy = -(l->y1 > l->y0 ? l->y1 - l->y0 : l->y0 - l->y1);
                    int16_t sx = l->x0 < l->x1 ? 1 : -1;
                    int16_t sy = l->y0 < l->y1 ? 1 : -1;
                    int16_t err = dx + dy;
                    int16_t cx = l->x0, cy = l->y0;
                    for (int i = 0; i < 1000; i++) {
                        gfx_pixel(cx, cy, l->color);
                        if (cx == l->x1 && cy == l->y1) break;
                        int16_t e2 = 2 * err;
                        if (e2 >= dy) { err += dy; cx += sx; }
                        if (e2 <= dx) { err += dx; cy += sy; }
                    }
                }
            }
            break;

        case IPP_MSG_DRAW_TEXT:
            if (e->len > sizeof(ipp_draw_text_header_t)) {
                const ipp_draw_text_header_t *t =
                    (const ipp_draw_text_header_t *)e->payload;
                const char *str =
                    (const char *)&e->payload[sizeof(ipp_draw_text_header_t)];
                const gfx_font_t *font =
                    (t->font_id == 0) ? FONT_SMALL : FONT_MAIN;
                gfx_draw_str(t->x, t->y, str, font, t->color, COL_BG);
            }
            break;

        case IPP_MSG_FB_FLIP:
            // Direct rendering — no framebuffer to flip
            break;
        }
    }
}

/* ──────────────────────────────────────────────────────────
 * Button Init + Scanning
 * ────────────────────────────────────────────────────────── */
static void init_buttons(void) {
    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_init(buttons[i].gpio);
        gpio_set_dir(buttons[i].gpio, GPIO_IN);
        gpio_pull_up(buttons[i].gpio);
    }
}

static void scan_buttons(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < BTN_COUNT; i++) {
        bool raw_pressed = !gpio_get(buttons[i].gpio);

        if (raw_pressed && !buttons[i].pressed) {
            if (now - buttons[i].last_change_ms < BTN_DEBOUNCE_MS)
                continue;
            buttons[i].pressed = true;
            buttons[i].press_time_ms = now;
            buttons[i].last_change_ms = now;
            buttons[i].held_sent = false;
            send_button_event(buttons[i].id, BTN_STATE_PRESSED, 0);

        } else if (raw_pressed && buttons[i].pressed) {
            uint32_t hold_duration = now - buttons[i].press_time_ms;

            if (hold_duration >= BTN_HOLD_MS && !buttons[i].held_sent) {
                buttons[i].held_sent = true;
                send_button_event(buttons[i].id, BTN_STATE_HELD, hold_duration);
            }

            if (buttons[i].id == BTN_ID_GRAY &&
                hold_duration >= BTN_POWER_OFF_MS) {
                if (charger_present) {
                    uint8_t addr = 0x11;
                    uint8_t reg = 0;
                    int ret;
                    ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                             &addr, 1, true);
                    if (ret >= 0)
                        ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                                &reg, 1, false);
                    bool vbus_good = (ret >= 0) && (reg & 0x80);
                    if (vbus_good) {
                        uint16_t vbus_mv = 2600 + (uint16_t)(reg & 0x7F) * 100;
                        char msg[48];
                        snprintf(msg, sizeof(msg),
                                 "Unplug USB to power off (%umV)", vbus_mv);
                        ui_toast_show(&ui_toast, msg, 2000);
                        ui_toast_draw(&ui_toast,
                                      to_ms_since_boot(get_absolute_time()));
                        buttons[i].held_sent = false;
                        buttons[i].press_time_ms = now;
                        continue;
                    }
                    uint slice = pwm_gpio_to_slice_num(PIN_TFT_BACKLIGHT);
                    pwm_set_chan_level(slice, PWM_CHAN_B, 0);
                    leds_enabled = false;
                    ws2812_show();

                    // Disable REG07 watchdog (bits 5:4 → 00) so it
                    // can't reset BATFET_DIS back to 0 after we lose power
                    addr = 0x07;
                    uint8_t reg07;
                    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                       &addr, 1, true);
                    i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                      &reg07, 1, false);
                    reg07 &= ~0x30;
                    uint8_t cmd07[2] = { 0x07, reg07 };
                    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                       cmd07, 2, false);

                    // REG09: BATFET_DLY (bit 3) = 1 for ~10s delayed
                    // disconnect, BATFET_DIS (bit 5) = 1 to cut power
                    addr = 0x09;
                    uint8_t reg09;
                    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                       &addr, 1, true);
                    i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                      &reg09, 1, false);
                    reg09 |= 0x28;
                    uint8_t cmd09[2] = { 0x09, reg09 };
                    i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER,
                                       cmd09, 2, false);
                } else {
                    uint slice = pwm_gpio_to_slice_num(PIN_TFT_BACKLIGHT);
                    pwm_set_chan_level(slice, PWM_CHAN_B, 0);
                }
                while (1) tight_loop_contents();
            }

        } else if (!raw_pressed && buttons[i].pressed) {
            if (now - buttons[i].last_change_ms < BTN_DEBOUNCE_MS)
                continue;
            buttons[i].pressed = false;
            buttons[i].last_change_ms = now;
            send_button_event(buttons[i].id, BTN_STATE_RELEASED,
                              now - buttons[i].press_time_ms);
        }
    }
}

static void send_button_event(uint8_t id, uint8_t state, uint32_t hold_ms) {
    ipp_button_event_t evt;
    evt.button_id = id;
    evt.state = state;
    evt.hold_ms = hold_ms;
    ipp_send_main(IPP_MSG_BUTTON_EVENT, &evt, sizeof(evt));
}

static void send_render_ready(void) {
    ipp_send_main(IPP_MSG_RENDER_READY, NULL, 0);
}

/* ──────────────────────────────────────────────────────────
 * LED Init (WS2812 via PIO)
 * ────────────────────────────────────────────────────────── */
static void ws2812_show(void) {
    if (!led_pio_inited) {
        led_pio = pio0;
        led_sm = pio_claim_unused_sm(led_pio, true);
        uint offset = pio_add_program(led_pio, &ws2812_program);
        ws2812_program_init(led_pio, led_sm, offset,
                            PIN_NEOPIXEL, 800000, false);
        gpio_set_outover(PIN_NEOPIXEL, GPIO_OVERRIDE_INVERT);
        led_pio_inited = true;
    }
    for (int i = 0; i < 7; i++) {
        uint32_t pixel = leds_enabled ? led_grb[i] : 0;
        pio_sm_put_blocking(led_pio, led_sm, pixel << 8u);
    }
}

static void init_leds(void) {
    gpio_init(PIN_NEOPIXEL);
    gpio_set_dir(PIN_NEOPIXEL, GPIO_OUT);
    gpio_put(PIN_NEOPIXEL, 0);
    sleep_ms(1);
    for (int i = 0; i < 7; i++)
        led_grb[i] = 0x020000;
    leds_enabled = true;
    ws2812_show();
}

/* ──────────────────────────────────────────────────────────
 * I2C Sensor Init
 * ────────────────────────────────────────────────────────── */
static void init_i2c_sensors(void) {
    i2c_init(LOCAL_I2C, LOCAL_I2C_FREQ);
    gpio_set_function(PIN_LOCAL_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_LOCAL_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_LOCAL_I2C_SDA);
    gpio_pull_up(PIN_LOCAL_I2C_SCL);

    uint8_t dummy;
    int ret;

    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_ACCEL, &dummy, 1, false);
    accel_present = (ret >= 0);

    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_IOEXP, &dummy, 1, false);
    ioexp_present = (ret >= 0);

    // Enable header power — one of these unknown GPIOs controls it
    static const uint8_t power_gpios[] = { 18, 19, 20, 21, 28 };
    for (int i = 0; i < 5; i++) {
        gpio_init(power_gpios[i]);
        gpio_set_dir(power_gpios[i], GPIO_OUT);
        gpio_put(power_gpios[i], 1);
    }

    if (ioexp_present) {
        // PCA9555 port 0 = header level shifter DIR pins
        // 0xDA = SCLK|CS|SPI_TX|UART_TX|UART_RTS as outputs (A→B)
        //        SPI_RX|UART_RX as inputs (B→A)
        // Port 1: 0xB8 = I2C pullup | GPIO25 | ANT_V1_2 | ANT_V1_1
        uint8_t cfg_both[2] = { 0x06, 0x00 };  // Port 0 config: all outputs
        i2c_write_blocking(LOCAL_I2C, I2C_ADDR_IOEXP, cfg_both, 2, false);
        uint8_t cfg1[2] = { 0x07, 0x00 };      // Port 1 config: all outputs
        i2c_write_blocking(LOCAL_I2C, I2C_ADDR_IOEXP, cfg1, 2, false);
        uint8_t out0[2] = { 0x02, 0xDA };
        i2c_write_blocking(LOCAL_I2C, I2C_ADDR_IOEXP, out0, 2, false);
        uint8_t out1[2] = { 0x03, 0xB8 };
        i2c_write_blocking(LOCAL_I2C, I2C_ADDR_IOEXP, out1, 2, false);
    }

    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &dummy, 1, false);
    charger_present = (ret >= 0);

    if (charger_present) {
        uint8_t addr = 0x02;
        uint8_t reg;
        ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &addr, 1, true);
        if (ret >= 0) {
            ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &reg, 1, false);
            if (ret >= 0 && (reg & 0xC0) != 0xC0) {
                uint8_t cmd[2] = { 0x02, (uint8_t)(reg | 0xC0) };
                i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, cmd, 2, false);
            }
        }
    }

    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_RTC, &dummy, 1, false);
    rtc_present = (ret >= 0);
}

/* ──────────────────────────────────────────────────────────
 * IPP UART Init
 * ────────────────────────────────────────────────────────── */
static void init_ipp_uart(void) {
    uart_init(IPP_UART, IPP_BAUD);

    gpio_set_function(PIN_IPP_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IPP_RX, GPIO_FUNC_UART);
    gpio_set_function(PIN_IPP_CTS, GPIO_FUNC_UART);
    gpio_set_function(PIN_IPP_RTS, GPIO_FUNC_UART);

    uart_set_hw_flow(IPP_UART, true, true);
    uart_set_format(IPP_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(IPP_UART, true);

    ipp_rx_init(&ipp_main_rx, on_main_frame, NULL);

    irq_set_exclusive_handler(IPP_UART_IRQ, uart0_irq_handler);
    irq_set_enabled(IPP_UART_IRQ, true);
    uart_set_irq_enables(IPP_UART, true, false);
}

/* ──────────────────────────────────────────────────────────
 * Boot Splash
 * ────────────────────────────────────────────────────────── */
static void show_splash(void) {
    gfx_clear(COL_BG);

    gfx_fill_rect(0, 0, TFT_WIDTH, STATUS_BAR_HEIGHT, COL_STATUS_BG);
    gfx_draw_str(4, 4, "AKHLUT CFW", FONT_SMALL, COL_HIGHLIGHT, COL_STATUS_BG);

    gfx_draw_str(80, 30, "FREEWILI 1 OG", FONT_MAIN, COL_HIGHLIGHT, COL_BG);
    gfx_hline(20, 44, 280, COL_DIM);
    gfx_draw_str(130, 50, "v0.1.0", FONT_MAIN, COL_TEXT, COL_BG);

    uint16_t y = 66;
    gfx_draw_str(20, y, "HARDWARE PROBE", FONT_MAIN, COL_WARN, COL_BG);
    y += 14;
    gfx_hline(20, y, 200, COL_DIM);
    y += 4;

    gfx_draw_str(20, y, "Accel LIS3DH:", FONT_MAIN, COL_TEXT, COL_BG);
    gfx_draw_str(130, y, accel_present ? "OK" : "NOT FOUND",
                 FONT_MAIN, accel_present ? COL_ACTIVE : COL_ERROR, COL_BG);
    y += 14;

    gfx_draw_str(20, y, "Charger BQ25892:", FONT_MAIN, COL_TEXT, COL_BG);
    gfx_draw_str(130, y, charger_present ? "OK" : "NOT FOUND",
                 FONT_MAIN, charger_present ? COL_ACTIVE : COL_ERROR, COL_BG);
    y += 14;

    gfx_draw_str(20, y, "RTC MCP7940:", FONT_MAIN, COL_TEXT, COL_BG);
    gfx_draw_str(130, y, rtc_present ? "OK" : "NOT FOUND",
                 FONT_MAIN, rtc_present ? COL_ACTIVE : COL_ERROR, COL_BG);
    y += 14;

    gfx_draw_str(20, y, "IO Exp PCA9555:", FONT_MAIN, COL_TEXT, COL_BG);
    gfx_draw_str(130, y, ioexp_present ? "OK" : "NOT FOUND",
                 FONT_MAIN, ioexp_present ? COL_ACTIVE : COL_ERROR, COL_BG);
    y += 18;

    gfx_draw_str(20, y, "BUTTONS", FONT_MAIN, COL_WARN, COL_BG);
    y += 14;
    gfx_hline(20, y, 200, COL_DIM);
    y += 4;
    gfx_draw_str(20, y, "5x GPIO OK", FONT_MAIN, COL_ACTIVE, COL_BG);
    y += 18;

    gfx_draw_str(20, y, "IPP LINK", FONT_MAIN, COL_WARN, COL_BG);
    y += 14;
    gfx_hline(20, y, 200, COL_DIM);
    y += 4;
    gfx_draw_str(20, y, "Waiting for Main...", FONT_MAIN, COL_DIM, COL_BG);

    gfx_fill_rect(0, TFT_HEIGHT - HINT_BAR_HEIGHT, TFT_WIDTH,
                   HINT_BAR_HEIGHT, COL_STATUS_BG);
    gfx_draw_str(4, TFT_HEIGHT - HINT_BAR_HEIGHT + 4, "Akhlut CFW",
                 FONT_SMALL, COL_DIM, COL_STATUS_BG);
}

/* ──────────────────────────────────────────────────────────
 * Core 1 — TFT DMA Flush Worker
 * ────────────────────────────────────────────────────────── */
static void core1_display_entry(void) {
    while (true) {
        uint32_t cmd = multicore_fifo_pop_blocking();

        switch (cmd) {
        case 0x01:
            // TODO: DMA transfer back-buffer to TFT via SPI
            multicore_fifo_push_blocking(0x01);
            break;

        default:
            multicore_fifo_push_blocking(0xFF);
            break;
        }
    }
}

/* ──────────────────────────────────────────────────────────
 * Main Entry Point
 * ────────────────────────────────────────────────────────── */
int main(void) {
    // Phase 1: TFT and graphics
    gfx_init();

    // Phase 2: Input + sensors (probe before splash so results are correct)
    init_buttons();
    init_leds();
    init_i2c_sensors();

    // Phase 2b: IR driver
    ir_driver_init();

    // Phase 3: Boot splash (shows sensor probe results)
    show_splash();

    // Phase 4: Init UI widgets
    ui_menu_init(&ui_menu);
    ui_status_bar_init(&ui_sb);
    ui_hint_bar_init(&ui_hb);
    ui_toast.active = false;
    ui_text_init(&ui_ts);

    // Phase 5: IPP link to Main
    init_ipp_uart();

    // Phase 6: Signal ready
    send_render_ready();

    // Phase 7: Launch Core 1 (TFT DMA worker)
    multicore_launch_core1(core1_display_entry);

    // Event Loop (Core 0)
    uint32_t last_button_scan = 0;
    uint32_t last_sensor_poll = 0;

    while (true) {
        if (pend_reboot_bootloader) {
            reset_usb_boot(0, 0);
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Scan buttons every ~10ms (100 Hz)
        if (now - last_button_scan >= 10) {
            scan_buttons();
            last_button_scan = now;
        }

        // Process buffered IPP messages from Main
        process_pending_messages();

        // IR receive — forward decoded codes to Main
        {
            ipp_ir_code_t ir;
            if (ir_driver_poll(&ir)) {
                ipp_send_main(IPP_MSG_IR_RECEIVED, &ir, sizeof(ir));
            }
        }

        // IR transmit — send buffered code
        if (pend_ir_send) {
            pend_ir_send = false;
            if (pend_ir_code.protocol == IR_PROTO_NEC) {
                ir_driver_send_nec(pend_ir_code.code, pend_ir_code.bits);
            }
        }

        if (pend_led_update) {
            pend_led_update = false;
            ws2812_show();
        }

        if (pend_i2c_scan) {
            pend_i2c_scan = false;
            ipp_i2c_scan_resp_t resp;
            resp.count = 0;
            for (uint8_t addr = 0x08; addr <= 0x77 && resp.count < 16; addr++) {
                uint8_t dummy;
                int ret = i2c_read_blocking(LOCAL_I2C, addr, &dummy, 1, false);
                if (ret >= 0)
                    resp.addrs[resp.count++] = addr;
            }
            ipp_send_main(IPP_MSG_I2C_SCAN_RESP, &resp,
                          1 + resp.count);
        }

        if (pend_ioexp_write) {
            pend_ioexp_write = false;
            uint8_t buf[2] = { pend_ioexp_reg, pend_ioexp_val };
            int ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_IOEXP,
                                         buf, 2, false);
            uint8_t resp[3] = { pend_ioexp_reg, pend_ioexp_val,
                                (ret == 2) ? 0 : 1 };
            ipp_send_main(IPP_MSG_IOEXP_RESP, resp, 3);
        }

        if (pend_ioexp_read) {
            pend_ioexp_read = false;
            uint8_t reg = pend_ioexp_read_reg;
            uint8_t val = 0;
            int ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_IOEXP,
                                         &reg, 1, true);
            if (ret == 1)
                ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_IOEXP,
                                        &val, 1, false);
            uint8_t resp[3] = { reg, val, (ret >= 0) ? 0 : 1 };
            ipp_send_main(IPP_MSG_IOEXP_RESP, resp, 3);
        }

        // Toast expiry — redraw underlying content when toast disappears
        if (ui_toast.active && now >= ui_toast.show_until_ms) {
            ui_toast.active = false;
            ui_sb.dirty = true;
            ui_hb.dirty = true;
            ui_status_bar_draw(&ui_sb);
            if (display_mode == DISPLAY_MODE_TEXT) {
                ui_ts.dirty = true;
                ui_text_draw(&ui_ts);
            } else {
                ui_menu.dirty = true;
                ui_menu_draw(&ui_menu);
            }
            ui_hint_bar_draw(&ui_hb);
        }

        // Poll sensors every ~1000ms
        if (now - last_sensor_poll >= 1000) {
            if (charger_present) {
                uint8_t reg;
                int ret;
                ipp_battery_data_t bat = {0};

                // BQ25892 REG0E: VBAT ADC (7 bits, 2304mV base, 20mV/bit)
                uint8_t addr = 0x0E;
                ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &addr, 1, true);
                if (ret >= 0) {
                    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &reg, 1, false);
                    if (ret >= 0) bat.vbatt_mv = 2304 + (uint16_t)(reg & 0x7F) * 20;
                }

                // BQ25892 REG11: bit 7 = VBUS_GD, bits 6:0 = VBUS ADC (2600mV base, 100mV/bit)
                addr = 0x11;
                ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &addr, 1, true);
                if (ret >= 0) {
                    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &reg, 1, false);
                    if (ret >= 0) {
                        bat.vbus_mv = 2600 + (uint16_t)(reg & 0x7F) * 100;
                        if (reg & 0x80)
                            bat.flags |= BATTERY_FLAG_VBUS_GD;
                    }
                }

                // BQ25892 REG0F: VSYS ADC (7 bits, 2304mV base, 20mV/bit)
                addr = 0x0F;
                ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &addr, 1, true);
                if (ret >= 0) {
                    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &reg, 1, false);
                    if (ret >= 0) bat.vsys_mv = 2304 + (uint16_t)(reg & 0x7F) * 20;
                }

                // BQ25892 REG12: ICHG ADC (7 bits, 0mA base, 50mA/bit)
                addr = 0x12;
                ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &addr, 1, true);
                if (ret >= 0) {
                    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &reg, 1, false);
                    if (ret >= 0) bat.ichg_ma = (uint16_t)(reg & 0x7F) * 50;
                }

                // BQ25892 REG0B: charge status
                addr = 0x0B;
                ret = i2c_write_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &addr, 1, true);
                if (ret >= 0) {
                    ret = i2c_read_blocking(LOCAL_I2C, I2C_ADDR_CHARGER, &reg, 1, false);
                    if (ret >= 0) {
                        uint8_t chrg_stat = (reg >> 3) & 0x03;
                        if (chrg_stat == 1 || chrg_stat == 2)
                            bat.flags |= BATTERY_FLAG_CHARGING;
                        if (chrg_stat == 3)
                            bat.flags |= BATTERY_FLAG_COMPLETE;
                    }
                }

                ipp_send_main(IPP_MSG_BATTERY_DATA, &bat, sizeof(bat));
            }
            last_sensor_poll = now;
        }

        tight_loop_contents();
    }

    return 0;
}
