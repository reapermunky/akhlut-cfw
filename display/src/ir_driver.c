/**
 * ir_driver.c — NEC IR Receive/Transmit Driver
 *
 * Akhlut CFW
 *
 * Receive: GPIO interrupt on PIN_IR_RX captures edge timestamps.
 * NEC protocol decode happens in ISR; decoded codes are placed
 * in a single-slot buffer for main-loop polling.
 *
 * Transmit: 38 kHz PWM on PIN_IR_TX with timed mark/space
 * bursts. Blocking — call from main loop only.
 *
 * NEC timing:
 *   Leader:  9000µs mark + 4500µs space
 *   Bit 1:   562µs mark + 1687µs space
 *   Bit 0:   562µs mark + 562µs space
 *   Stop:    562µs mark
 *   Repeat:  9000µs mark + 2250µs space + 562µs mark
 */

#include "ir_driver.h"
#include "board_display.h"
#include "pico/stdlib.h"
#include "pico/platform.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/timer.h"

/* ──────────────────────────────────────────────────────────
 * NEC Decoder State (ISR context)
 * ────────────────────────────────────────────────────────── */
#define NEC_BITS 32

typedef enum {
    IR_IDLE,
    IR_LEADER_MARK,
    IR_LEADER_SPACE,
    IR_DATA,
} ir_rx_state_t;

static volatile ir_rx_state_t rx_state = IR_IDLE;
static volatile uint64_t      rx_last_edge_us;
static volatile uint32_t      rx_data;
static volatile uint8_t       rx_bit_count;

static volatile bool          rx_ready = false;
static volatile ipp_ir_code_t rx_code;

static inline bool in_range(uint32_t val, uint32_t center, uint32_t tolerance) {
    return val >= (center - tolerance) && val <= (center + tolerance);
}

static void __not_in_flash_func(ir_rx_gpio_callback)(uint gpio, uint32_t events) {
    if (gpio != PIN_IR_RX) return;

    uint64_t now_us = time_us_64();
    uint32_t elapsed = (uint32_t)(now_us - rx_last_edge_us);
    rx_last_edge_us = now_us;

    bool falling = (events & GPIO_IRQ_EDGE_FALL) != 0;
    bool rising  = (events & GPIO_IRQ_EDGE_RISE) != 0;

    switch (rx_state) {
    case IR_IDLE:
        if (falling) {
            rx_state = IR_LEADER_MARK;
        }
        break;

    case IR_LEADER_MARK:
        if (rising) {
            if (in_range(elapsed, 9000, 1500)) {
                rx_state = IR_LEADER_SPACE;
            } else {
                rx_state = IR_IDLE;
            }
        }
        break;

    case IR_LEADER_SPACE:
        if (falling) {
            if (in_range(elapsed, 4500, 1000)) {
                rx_data = 0;
                rx_bit_count = 0;
                rx_state = IR_DATA;
            } else if (in_range(elapsed, 2250, 500)) {
                // NEC repeat — ignore for now
                rx_state = IR_IDLE;
            } else {
                rx_state = IR_IDLE;
            }
        }
        break;

    case IR_DATA:
        if (rising) {
            // Mark ended — don't update rx_last_edge_us so that
            // the next falling edge measures mark+space combined
            return;
        } else if (falling) {
            // elapsed = mark + space combined (since we skip updating on rising)
            if (in_range(elapsed, 1125, 400)) {
                // Bit 0: 562µs mark + 562µs space ≈ 1125µs total
                rx_data >>= 1;
                rx_bit_count++;
            } else if (in_range(elapsed, 2250, 400)) {
                // Bit 1: 562µs mark + 1687µs space ≈ 2250µs total
                rx_data >>= 1;
                rx_data |= 0x80000000;
                rx_bit_count++;
            } else {
                rx_state = IR_IDLE;
                break;
            }

            if (rx_bit_count >= NEC_BITS) {
                rx_code.protocol = IR_PROTO_NEC;
                rx_code.code = rx_data;
                rx_code.bits = NEC_BITS;
                rx_ready = true;
                rx_state = IR_IDLE;
            }
        }
        break;
    }
}

/* ──────────────────────────────────────────────────────────
 * Public API
 * ────────────────────────────────────────────────────────── */
void ir_driver_init(void) {
    // RX: input with pull-up (IR receiver output is active-low)
    gpio_init(PIN_IR_RX);
    gpio_set_dir(PIN_IR_RX, GPIO_IN);
    gpio_pull_up(PIN_IR_RX);

    rx_last_edge_us = time_us_64();
    rx_state = IR_IDLE;

    gpio_set_irq_enabled_with_callback(
        PIN_IR_RX,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
        true,
        ir_rx_gpio_callback
    );

    // TX: GPIO output (not PWM — pin 9 shares PWM slice 4 with backlight pin 25)
    gpio_init(PIN_IR_TX);
    gpio_set_dir(PIN_IR_TX, GPIO_OUT);
    gpio_put(PIN_IR_TX, 0);
}

bool ir_driver_poll(ipp_ir_code_t *out) {
    if (!rx_ready) return false;
    rx_ready = false;
    out->protocol = rx_code.protocol;
    out->code     = rx_code.code;
    out->bits     = rx_code.bits;
    return true;
}

static void ir_tx_mark(uint32_t us) {
    uint64_t end = time_us_64() + us;
    while (time_us_64() < end) {
        gpio_put(PIN_IR_TX, 1);
        busy_wait_us_32(13);
        gpio_put(PIN_IR_TX, 0);
        busy_wait_us_32(13);
    }
}

static void ir_tx_space(uint32_t us) {
    gpio_put(PIN_IR_TX, 0);
    busy_wait_us_32(us);
}

void ir_driver_send_nec(uint32_t code, uint8_t bits) {
    // Leader
    ir_tx_mark(9000);
    ir_tx_space(4500);

    // Data (LSB first)
    for (uint8_t i = 0; i < bits; i++) {
        ir_tx_mark(562);
        if (code & (1u << i)) {
            ir_tx_space(1687);
        } else {
            ir_tx_space(562);
        }
    }

    // Stop bit
    ir_tx_mark(562);
    ir_tx_space(562);

    gpio_put(PIN_IR_TX, 0);
}
