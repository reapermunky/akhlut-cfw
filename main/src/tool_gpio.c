/**
 * tool_gpio.c — GPIO Monitor / Control Tool
 *
 * Akhlut CFW
 *
 * Read and toggle the external GPIO header pins in real time.
 * Uses EXT_GPIO_24 (out), EXT_GPIO_25 (out), EXT_GPIO_26 (in).
 * Also shows the external I2C and SPI bus state.
 */

#include "tool.h"
#include "board.h"
#include "ipp_defs.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>
#include <string.h>

#define GPIO_PIN_COUNT   3
#define GPIO_REFRESH_MS  200

typedef struct {
    uint8_t  selected;
    bool     pin_state[GPIO_PIN_COUNT];
    uint32_t last_refresh;
} gpio_state_t;

_Static_assert(sizeof(gpio_state_t) <= TOOL_STATE_POOL_SIZE,
               "gpio_state_t exceeds state pool");

typedef struct {
    const char *name;
    uint8_t     gpio;
    bool        is_output;
} gpio_pin_def_t;

static const gpio_pin_def_t pins[GPIO_PIN_COUNT] = {
    { "GP27 (Hdr 3)",  PIN_EXT_GPIO_24, true  },
    { "GP25 (Hdr 17)", PIN_EXT_GPIO_25, true  },
    { "GP26 (Hdr 14)", PIN_EXT_GPIO_26, false },
};

/* ──────────────────────────────────────────────────────────
 * Display
 * ────────────────────────────────────────────────────────── */
static void read_pins(gpio_state_t *s) {
    for (int i = 0; i < GPIO_PIN_COUNT; i++) {
        s->pin_state[i] = gpio_get(pins[i].gpio);
    }
}

static void show_gpio_screen(gpio_state_t *s) {
    char lb[12][54];
    const char *lp[12];
    int n = 0;

    snprintf(lb[n], 54, "  GPIO MONITOR");
    lp[n] = lb[n]; n++;

    lp[n] = ""; n++;

    for (int i = 0; i < GPIO_PIN_COUNT; i++) {
        const char *dir = pins[i].is_output ? "OUT" : "IN ";
        const char *val = s->pin_state[i] ? "HIGH" : "LOW ";
        const char *sel = (i == s->selected) ? "> " : "  ";

        snprintf(lb[n], 54, "%s%-14s %s  %s", sel,
                 pins[i].name, dir, val);
        lp[n] = lb[n]; n++;
    }

    lp[n] = ""; n++;
    lp[n] = "  [GREEN] Toggle output"; n++;
    lp[n] = "  [RED] Exit"; n++;

    tool_send_text_screen(lp, (uint8_t)n);
}

/* ──────────────────────────────────────────────────────────
 * Tool Callbacks
 * ────────────────────────────────────────────────────────── */
static tool_result_t gpio_enter(tool_ctx_t *ctx) {
    gpio_state_t *s = (gpio_state_t *)ctx->state;
    s->selected = 0;
    s->last_refresh = 0;

    tool_send_status_bar("GPIO");
    read_pins(s);
    show_gpio_screen(s);

    return TOOL_OK;
}

static tool_result_t gpio_update(tool_ctx_t *ctx) {
    gpio_state_t *s = (gpio_state_t *)ctx->state;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - s->last_refresh >= GPIO_REFRESH_MS) {
        s->last_refresh = now;
        read_pins(s);
        show_gpio_screen(s);
    }

    return TOOL_OK;
}

static tool_result_t gpio_on_button(tool_ctx_t *ctx,
                                     uint8_t btn_id,
                                     uint8_t btn_state) {
    gpio_state_t *s = (gpio_state_t *)ctx->state;

    if (btn_state != BTN_STATE_PRESSED) return TOOL_OK;

    switch (btn_id) {
    case IPP_BTN_YELLOW:
        if (s->selected > 0) s->selected--;
        else s->selected = GPIO_PIN_COUNT - 1;
        show_gpio_screen(s);
        break;
    case IPP_BTN_BLUE:
        if (s->selected < GPIO_PIN_COUNT - 1) s->selected++;
        else s->selected = 0;
        show_gpio_screen(s);
        break;
    case IPP_BTN_GREEN:
        if (pins[s->selected].is_output) {
            bool cur = gpio_get(pins[s->selected].gpio);
            gpio_put(pins[s->selected].gpio, !cur);
            read_pins(s);
            show_gpio_screen(s);
        } else {
            tool_send_toast("Input pin (read-only)", 1000);
        }
        break;
    case IPP_BTN_RED:
        return TOOL_EXIT;
    }

    return TOOL_OK;
}

static void gpio_exit(tool_ctx_t *ctx) {
    (void)ctx;
}

const tool_desc_t tool_gpio_desc = {
    .name         = "GPIO",
    .icon_id      = 5,
    .requires     = 0,
    .state_size   = sizeof(gpio_state_t),
    .enter        = gpio_enter,
    .update       = gpio_update,
    .on_button    = gpio_on_button,
    .exit         = gpio_exit,
    .on_orca_msg  = NULL,
};
