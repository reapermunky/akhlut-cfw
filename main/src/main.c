/**
 * main.c — FreeWili 1 Main RP2040 Firmware
 *
 * Akhlut CFW
 *
 * Core 0: Tool engine, IPP protocol, radio drivers, all real-time I/O
 * Core 1: Flash filesystem I/O (isolated to prevent XIP stalls)
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/uart.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"

#include "board.h"
#include "ipp_defs.h"
#include "ipp.h"
#include "tool.h"
#include "cc1101.h"
#include "pico/flash.h"

extern const tool_desc_t tool_subghz_desc;
extern const tool_desc_t tool_ghost_desc;
extern const tool_desc_t tool_bus_desc;
extern const tool_desc_t tool_gpio_desc;
extern const tool_desc_t tool_ir_desc;
extern const tool_desc_t tool_settings_desc;
extern const tool_desc_t tool_about_desc;
extern const tool_desc_t tool_apps_desc;

/* ──────────────────────────────────────────────────────────
 * Forward Declarations
 * ────────────────────────────────────────────────────────── */
static void boot_reset_display(void);
static void init_ipp_uart(void);
static void init_radio_spi(void);
static bool probe_radio(uint8_t cs_pin, uint8_t *partnum, uint8_t *version);
static void init_fpga(void);
static void init_external_buses(void);
static bool probe_orca(void);
static void init_orca_irq(void);
static void init_host_serial(void);
static bool check_safe_mode(void);
static void register_tools(void);
static void send_main_menu(void);
static void return_to_menu(void);
static void core1_entry(void);

extern void settings_boot_apply(void);
extern uint8_t settings_get_brightness(void);
extern uint8_t settings_get_sleep_min(void);

#include "fs.h"

static void __not_in_flash_func(uart0_irq_handler)(void);
static void __not_in_flash_func(uart1_irq_handler)(void);
static void __not_in_flash_func(on_display_frame)(const ipp_frame_t *frame, void *user_data);
static void __not_in_flash_func(on_orca_frame)(const ipp_frame_t *frame, void *user_data);

/* ──────────────────────────────────────────────────────────
 * Global State
 * ────────────────────────────────────────────────────────── */

// IPP
static ipp_rx_ctx_t     ipp_display_rx;
static uint8_t          ipp_tx_buf[IPP_MAX_FRAME_SIZE];
static volatile uint8_t ipp_seq_display = 0;
static volatile uint8_t ipp_seq_orca = 0;

// Hardware status
static bool radio1_present = false;
static bool radio2_present = false;
static bool fpga_configured = false;
static bool orca_present = false;
static bool safe_mode = false;
static bool display_ready = false;

// Battery state (updated from Display via IPP_MSG_BATTERY_DATA)
volatile uint8_t  battery_pct = 0;
static volatile bool     battery_charging = false;
static volatile bool     battery_dirty = false;
volatile uint16_t battery_vbatt_mv = 0;
static volatile uint16_t battery_vbus_mv = 0;
static volatile uint16_t battery_ichg_ma = 0;
volatile uint8_t  battery_flags_raw = 0;

// IR received pending buffer (Display→Main)
volatile bool        ir_pending = false;
volatile ipp_ir_code_t ir_pending_code;
static volatile bool        i2c_scan_pending = false;
static ipp_i2c_scan_resp_t  i2c_scan_resp_buf;

volatile bool        ioexp_resp_pending = false;
volatile uint8_t     ioexp_resp_reg;
volatile uint8_t     ioexp_resp_val;
volatile uint8_t     ioexp_resp_status;

// Accelerometer data (Display→Main, cached)
volatile bool           accel_data_valid = false;
volatile ipp_accel_data_t accel_last;

// Menu state
static uint8_t menu_selected = 0;

// Auto-sleep
uint32_t last_activity_ms = 0;
static bool display_asleep = false;

// Button event ring buffer (ISR producer, main loop consumer)
#define BTN_EVT_SIZE 8

typedef struct {
    uint8_t id;
    uint8_t state;
} btn_evt_t;

static volatile btn_evt_t btn_evts[BTN_EVT_SIZE];
static volatile uint8_t   btn_evt_head = 0;
static volatile uint8_t   btn_evt_tail = 0;

// Raw UART1 circular buffer for GhostESP text mode
#define ORCA_RAW_BUF_SIZE 1024
static volatile uint8_t  orca_raw_buf[ORCA_RAW_BUF_SIZE];
static volatile uint16_t orca_raw_wr = 0;
static volatile uint16_t orca_raw_rd = 0;
volatile bool tool_orca_raw_mode = false;

uint16_t tool_orca_raw_available(void) {
    uint16_t wr = orca_raw_wr;
    uint16_t rd = orca_raw_rd;
    return (wr >= rd) ? (wr - rd) : (ORCA_RAW_BUF_SIZE - rd + wr);
}

uint16_t tool_orca_raw_read(uint8_t *buf, uint16_t max) {
    uint16_t count = 0;
    while (count < max && orca_raw_rd != orca_raw_wr) {
        buf[count++] = orca_raw_buf[orca_raw_rd];
        orca_raw_rd = (orca_raw_rd + 1) % ORCA_RAW_BUF_SIZE;
    }
    return count;
}

void tool_orca_raw_flush(void) {
    orca_raw_rd = orca_raw_wr;
}

void tool_orca_raw_send(const char *text) {
    uart_write_blocking(EXT_UART, (const uint8_t *)text, strlen(text));
}

// Orca message ring buffer (ISR producer, main loop consumer)
#define ORCA_MSG_SLOTS 4
#define ORCA_MSG_MAX   256

typedef struct {
    uint8_t  type;
    uint16_t len;
    uint8_t  payload[ORCA_MSG_MAX];
} orca_pending_msg_t;

static ipp_rx_ctx_t            ipp_orca_rx;
static volatile orca_pending_msg_t orca_pending[ORCA_MSG_SLOTS];
static volatile uint8_t        orca_pend_head = 0;
static volatile uint8_t        orca_pend_tail = 0;

/* All tool descriptors are defined in their own source files
 * and registered in register_tools(). No stubs remain. */

volatile bool tool_wasm_force_stop = false;

bool tool_poll_button(uint8_t *out_id) {
    while (btn_evt_tail != btn_evt_head) {
        uint8_t id = btn_evts[btn_evt_tail].id;
        uint8_t state = btn_evts[btn_evt_tail].state;
        btn_evt_tail = (btn_evt_tail + 1) % BTN_EVT_SIZE;
        if (state == BTN_STATE_HELD && id == IPP_BTN_RED) {
            tool_wasm_force_stop = true;
        }
        if (state == BTN_STATE_PRESSED) {
            *out_id = id;
            return true;
        }
    }
    return false;
}

void tool_flush_buttons(void) {
    btn_evt_tail = btn_evt_head;
}

/* ──────────────────────────────────────────────────────────
 * IPP Transmit
 * ────────────────────────────────────────────────────────── */
static void ipp_send_display(uint8_t type, const void *payload, uint16_t len) {
    size_t frame_len = ipp_build(ipp_seq_display++, type, payload, len,
                                 ipp_tx_buf, sizeof(ipp_tx_buf));
    if (frame_len > 0) {
        uart_write_blocking(IPP_UART, ipp_tx_buf, frame_len);
    }
}

static void ipp_send_orca(uint8_t type, const void *payload, uint16_t len) {
    size_t frame_len = ipp_build(ipp_seq_orca++, type, payload, len,
                                 ipp_tx_buf, sizeof(ipp_tx_buf));
    if (frame_len > 0) {
        uart_write_blocking(EXT_UART, ipp_tx_buf, frame_len);
    }
}

/* ──────────────────────────────────────────────────────────
 * IPP Receive Callback (ISR context)
 * ────────────────────────────────────────────────────────── */
static void __not_in_flash_func(on_display_frame)(const ipp_frame_t *frame, void *user_data) {
    switch (frame->type) {

    case IPP_MSG_RENDER_READY:
        display_ready = true;
        break;

    case IPP_MSG_BUTTON_EVENT:
        if (frame->length >= sizeof(ipp_button_event_t)) {
            const ipp_button_event_t *evt =
                (const ipp_button_event_t *)frame->payload;
            uint8_t next = (btn_evt_head + 1) % BTN_EVT_SIZE;
            if (next != btn_evt_tail) {
                btn_evts[btn_evt_head].id = evt->button_id;
                btn_evts[btn_evt_head].state = evt->state;
                btn_evt_head = next;
            }
        }
        break;

    case IPP_MSG_BATTERY_DATA:
        if (frame->length >= sizeof(ipp_battery_data_t)) {
            const ipp_battery_data_t *bat =
                (const ipp_battery_data_t *)frame->payload;
            uint16_t mv = bat->vbatt_mv;
            if (mv >= 4150) battery_pct = 100;
            else if (mv <= 3300) battery_pct = 0;
            else battery_pct = (uint8_t)((mv - 3300) * 100 / 850);
            battery_charging = (bat->flags & BATTERY_FLAG_CHARGING) != 0;
            battery_dirty = true;
            battery_vbatt_mv = bat->vbatt_mv;
            battery_vbus_mv = bat->vbus_mv;
            battery_ichg_ma = bat->ichg_ma;
            battery_flags_raw = bat->flags;
        }
        break;

    case IPP_MSG_ACCEL_DATA:
        if (frame->length >= sizeof(ipp_accel_data_t)) {
            const ipp_accel_data_t *acc =
                (const ipp_accel_data_t *)frame->payload;
            accel_last.x = acc->x;
            accel_last.y = acc->y;
            accel_last.z = acc->z;
            accel_last.g_total = acc->g_total;
            accel_last.temp_c_x10 = acc->temp_c_x10;
            accel_data_valid = true;
        }
        break;

    case IPP_MSG_RTC_TIME:
        break;

    case IPP_MSG_IR_RECEIVED:
        if (frame->length >= sizeof(ipp_ir_code_t)) {
            const ipp_ir_code_t *ir =
                (const ipp_ir_code_t *)frame->payload;
            ir_pending_code.protocol = ir->protocol;
            ir_pending_code.code     = ir->code;
            ir_pending_code.bits     = ir->bits;
            ir_pending = true;
        }
        break;

    case IPP_MSG_I2C_SCAN_RESP:
        if (frame->length >= 1) {
            uint8_t count = frame->payload[0];
            if (count > 16) count = 16;
            i2c_scan_resp_buf.count = count;
            for (uint8_t i = 0; i < count && (i + 1) < frame->length; i++)
                i2c_scan_resp_buf.addrs[i] = frame->payload[1 + i];
            i2c_scan_pending = true;
        }
        break;

    case IPP_MSG_IOEXP_RESP:
        if (frame->length >= 3) {
            ioexp_resp_reg    = frame->payload[0];
            ioexp_resp_val    = frame->payload[1];
            ioexp_resp_status = frame->payload[2];
            ioexp_resp_pending = true;
        }
        break;

    default:
        break;
    }
}

/* ──────────────────────────────────────────────────────────
 * UART0 ISR — Display Link (SRAM)
 * ────────────────────────────────────────────────────────── */
static void __not_in_flash_func(uart0_irq_handler)(void) {
    while (uart_is_readable(IPP_UART)) {
        uint8_t byte = uart_getc(IPP_UART);
        ipp_rx_feed(&ipp_display_rx, byte);
    }
}

/* ──────────────────────────────────────────────────────────
 * UART1 ISR — Orca Link (SRAM)
 * ────────────────────────────────────────────────────────── */
static void __not_in_flash_func(on_orca_frame)(const ipp_frame_t *frame, void *user_data) {
    (void)user_data;
    uint8_t next = (orca_pend_head + 1) % ORCA_MSG_SLOTS;
    if (next == orca_pend_tail) return;

    volatile orca_pending_msg_t *slot = &orca_pending[orca_pend_head];
    slot->type = frame->type;
    uint16_t clen = frame->length > ORCA_MSG_MAX
                    ? ORCA_MSG_MAX : frame->length;
    slot->len = clen;
    for (uint16_t i = 0; i < clen; i++)
        slot->payload[i] = frame->payload[i];
    orca_pend_head = next;
}

static void __not_in_flash_func(uart1_irq_handler)(void) {
    while (uart_is_readable(EXT_UART)) {
        uint8_t byte = uart_getc(EXT_UART);
        if (tool_orca_raw_mode) {
            uint16_t next = (orca_raw_wr + 1) % ORCA_RAW_BUF_SIZE;
            if (next != orca_raw_rd) {
                orca_raw_buf[orca_raw_wr] = byte;
                orca_raw_wr = next;
            }
        } else {
            ipp_rx_feed(&ipp_orca_rx, byte);
        }
    }
}

/* ──────────────────────────────────────────────────────────
 * Init: Orca IRQ (called after successful probe)
 * ────────────────────────────────────────────────────────── */
static void init_orca_irq(void) {
    ipp_rx_init(&ipp_orca_rx, on_orca_frame, NULL);
    irq_set_exclusive_handler(EXT_UART_IRQ, uart1_irq_handler);
    irq_set_enabled(EXT_UART_IRQ, true);
    uart_set_irq_enables(EXT_UART, true, false);
}

/* ──────────────────────────────────────────────────────────
 * Boot: Reset Display RP2040
 * ────────────────────────────────────────────────────────── */
static void boot_reset_display(void) {
    gpio_init(PIN_DISPLAY_RUN);
    gpio_set_dir(PIN_DISPLAY_RUN, GPIO_OUT);

    gpio_put(PIN_DISPLAY_RUN, 0);
    sleep_ms(10);

    gpio_put(PIN_DISPLAY_RUN, 1);
}

/* ──────────────────────────────────────────────────────────
 * Init: IPP UART
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

    ipp_rx_init(&ipp_display_rx, on_display_frame, NULL);

    irq_set_exclusive_handler(IPP_UART_IRQ, uart0_irq_handler);
    irq_set_enabled(IPP_UART_IRQ, true);
    uart_set_irq_enables(IPP_UART, true, false);
}

/* ──────────────────────────────────────────────────────────
 * Init: CC1101 Radio Bus (SPI0)
 * ────────────────────────────────────────────────────────── */
static void init_radio_spi(void) {
    spi_init(RADIO_SPI, 5000000);

    gpio_set_function(PIN_RADIO_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_RADIO_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_RADIO_MOSI, GPIO_FUNC_SPI);

    gpio_init(PIN_RADIO1_CS);
    gpio_set_dir(PIN_RADIO1_CS, GPIO_OUT);
    gpio_put(PIN_RADIO1_CS, 1);

    gpio_init(PIN_RADIO2_CS);
    gpio_set_dir(PIN_RADIO2_CS, GPIO_OUT);
    gpio_put(PIN_RADIO2_CS, 1);

    gpio_init(PIN_RADIO1_GDO0);
    gpio_set_dir(PIN_RADIO1_GDO0, GPIO_IN);
    gpio_init(PIN_RADIO1_GDO2);
    gpio_set_dir(PIN_RADIO1_GDO2, GPIO_IN);
    gpio_init(PIN_RADIO2_GDO0);
    gpio_set_dir(PIN_RADIO2_GDO0, GPIO_IN);
    gpio_init(PIN_RADIO2_GDO2);
    gpio_set_dir(PIN_RADIO2_GDO2, GPIO_IN);
}

static bool probe_radio(uint8_t cs_pin, uint8_t *partnum, uint8_t *version) {
    uint8_t tx[2], rx[2];

    tx[0] = 0x30 | 0xC0;
    tx[1] = 0x00;

    gpio_put(cs_pin, 0);
    sleep_us(1);
    spi_write_read_blocking(RADIO_SPI, tx, rx, 2);
    gpio_put(cs_pin, 1);
    *partnum = rx[1];

    tx[0] = 0x31 | 0xC0;
    tx[1] = 0x00;

    gpio_put(cs_pin, 0);
    sleep_us(1);
    spi_write_read_blocking(RADIO_SPI, tx, rx, 2);
    gpio_put(cs_pin, 1);
    *version = rx[1];

    return (*partnum == 0x00 && *version == 0x14);
}

/* ──────────────────────────────────────────────────────────
 * Init: FPGA (iCE40UP5K)
 *
 * The iCE40UP5K has no NVCM bitstream on this board.
 * We load the pass-through bitstream via SPI slave config.
 *
 * Wiring (RP2040 GP → FPGA pin):
 *   GP12 → pin 14 (SPI_SI)     data into FPGA
 *   GP13 → pin 16 (SPI_SS_B)   chip select (active low)
 *   GP14 → pin 15 (SPI_SCK)    clock
 *   GP24 → CDONE               config done (input)
 *   GP29 → CRESET_B            config reset (active low)
 *   GP23 → pin 37              reference clock (31.25 MHz)
 *
 * GP12 is RP2040 SPI1-RX but FPGA SPI_SI needs data IN,
 * so we bit-bang instead of using hardware SPI.
 * ────────────────────────────────────────────────────────── */
#include "fpga_bitstream.h"

#define FPGA_SPI_SI   PIN_EXT_SPI_MISO  // GP12
#define FPGA_SPI_SS   PIN_EXT_SPI_CS    // GP13
#define FPGA_SPI_SCK  PIN_EXT_SPI_SCK   // GP14

static void init_fpga(void) {
    gpio_init(PIN_FPGA_CRESET);
    gpio_set_dir(PIN_FPGA_CRESET, GPIO_OUT);

    gpio_init(PIN_FPGA_CDONE);
    gpio_set_dir(PIN_FPGA_CDONE, GPIO_IN);

    // Start 31.25 MHz reference clock (125 MHz / 4)
    gpio_set_function(PIN_FPGA_CLK, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_FPGA_CLK);
    pwm_set_wrap(slice, 3);
    pwm_set_chan_level(slice, PWM_CHAN_B, 2);
    pwm_set_enabled(slice, true);

    // Manual CS on GP13 (FPGA SPI_SS_B)
    gpio_init(FPGA_SPI_SS);
    gpio_set_dir(FPGA_SPI_SS, GPIO_OUT);
    gpio_put(FPGA_SPI_SS, 1);

    // 1. Assert CRESET low, then SS low → slave mode
    gpio_put(PIN_FPGA_CRESET, 0);
    sleep_us(10);
    gpio_put(FPGA_SPI_SS, 0);
    sleep_ms(1);

    // 2. Release CRESET, wait for internal clear (≥800μs for UP5K)
    gpio_put(PIN_FPGA_CRESET, 1);
    sleep_ms(3);

    // 3. Init hardware SPI1 at 1 MHz for bitstream loading
    //    MOSI=GP15, SCK=GP14 — GP13 is manual CS
    spi_init(EXT_SPI, 1000000);
    gpio_set_function(PIN_EXT_SPI_MOSI, GPIO_FUNC_SPI);  // GP15 TX
    gpio_set_function(PIN_EXT_SPI_SCK,  GPIO_FUNC_SPI);  // GP14 SCK

    // 4. Send 1 dummy byte to flush shift register
    uint8_t dummy = 0x00;
    spi_write_blocking(EXT_SPI, &dummy, 1);

    // 5. Send bitstream
    spi_write_blocking(EXT_SPI, fpga_bitstream, FPGA_BITSTREAM_LEN);

    // 6. Send trailing clocks (25 bytes = 200 clocks)
    uint8_t trail[25];
    memset(trail, 0x00, sizeof(trail));
    spi_write_blocking(EXT_SPI, trail, sizeof(trail));

    // 7. Deinit SPI1 (init_external_buses will reconfigure)
    spi_deinit(EXT_SPI);

    // 8. Release SS
    gpio_put(FPGA_SPI_SS, 1);

    // 9. Check CDONE
    sleep_ms(10);
    fpga_configured = gpio_get(PIN_FPGA_CDONE);

    if (fpga_configured) {
        printf("[FPGA] Bitstream loaded — CDONE high\n");
    } else {
        // Try alternate pin: bit-bang on GP12 (MISO) in case SPI_SI is there
        printf("[FPGA] HW SPI failed (CDONE=0), trying GP12 bit-bang...\n");

        gpio_init(FPGA_SPI_SI);
        gpio_set_dir(FPGA_SPI_SI, GPIO_OUT);
        gpio_put(FPGA_SPI_SI, 0);

        gpio_init(FPGA_SPI_SCK);
        gpio_set_dir(FPGA_SPI_SCK, GPIO_OUT);
        gpio_put(FPGA_SPI_SCK, 0);

        // Reset FPGA again for slave config
        gpio_put(PIN_FPGA_CRESET, 0);
        sleep_us(10);
        gpio_put(FPGA_SPI_SS, 0);
        sleep_ms(1);
        gpio_put(PIN_FPGA_CRESET, 1);
        sleep_ms(3);

        // Dummy clocks
        for (int i = 0; i < 8; i++) {
            gpio_put(FPGA_SPI_SI, 0);
            busy_wait_at_least_cycles(30);
            gpio_put(FPGA_SPI_SCK, 1);
            busy_wait_at_least_cycles(30);
            gpio_put(FPGA_SPI_SCK, 0);
        }

        // Send bitstream via GP12
        for (size_t i = 0; i < FPGA_BITSTREAM_LEN; i++) {
            uint8_t byte = fpga_bitstream[i];
            for (int bit = 7; bit >= 0; bit--) {
                gpio_put(FPGA_SPI_SI, (byte >> bit) & 1);
                busy_wait_at_least_cycles(30);
                gpio_put(FPGA_SPI_SCK, 1);
                busy_wait_at_least_cycles(30);
                gpio_put(FPGA_SPI_SCK, 0);
            }
        }

        // Trailing clocks
        for (int i = 0; i < 200; i++) {
            gpio_put(FPGA_SPI_SI, 0);
            busy_wait_at_least_cycles(30);
            gpio_put(FPGA_SPI_SCK, 1);
            busy_wait_at_least_cycles(30);
            gpio_put(FPGA_SPI_SCK, 0);
        }

        gpio_put(FPGA_SPI_SS, 1);
        sleep_ms(10);
        fpga_configured = gpio_get(PIN_FPGA_CDONE);
        printf("[FPGA] GP12 bit-bang: CDONE=%d → %s\n",
               fpga_configured, fpga_configured ? "CONFIGURED" : "FAILED");
    }

    // Leave SPI pins as GPIO — init_external_buses() reconfigures them
}

/* ──────────────────────────────────────────────────────────
 * Init: FPGA IO Direction Register
 *
 * The iCE40's io_buffer block gates ALL breakout signals.
 * Without this write, the FPGA blocks signals in both
 * directions — this is why the UART header path was dead.
 *
 * FW1 opcodes: write=0x40, read=0x20, register addr=0x01
 * SPI rate: 3.25 MHz (different from config's 5 MHz)
 * CS: GP13 (manual GPIO, not hardware SPI CS)
 *
 * Direction word byte 0 (1 = Main output):
 *   bit 0: UART_RTS  (GP11)  = 1
 *   bit 1: UART_RX   (GP9)   = 0
 *   bit 2: UART_TX   (GP8)   = 1
 *   bit 3: SPI_TX    (GP15)  = 1
 *   bit 4: SPI_RX    (GP12)  = 0
 *   bit 5: SPI_CS    (GP13)  = 1
 *   bit 6: SPI_SCLK  (GP14)  = 1
 *   bit 7: UART_CTS  (GP10)  = 0
 *   → 0x6D
 *
 * Byte 1: (gpio26_out << 1) | gpio27_out = (0 << 1) | 1 = 0x01
 * ────────────────────────────────────────────────────────── */
static bool fpga_write_io_dir(void) {
    if (!fpga_configured) {
        printf("[FPGA] io_dir: skipped (FPGA not configured)\n");
        return false;
    }

    const uint8_t dir_data[2] = { 0x6D, 0x01 };
    const uint8_t cmd_wr = 0x41;  // 0x40 | 0x01
    const uint8_t cmd_rd = 0x21;  // 0x20 | 0x01

    // Switch GP13 to manual GPIO for CS control
    gpio_init(PIN_EXT_SPI_CS);
    gpio_set_dir(PIN_EXT_SPI_CS, GPIO_OUT);
    gpio_put(PIN_EXT_SPI_CS, 1);

    // Ensure MISO/SCK/MOSI are SPI function
    gpio_set_function(PIN_EXT_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_SPI_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_SPI_MOSI, GPIO_FUNC_SPI);

    // Set SPI rate to 3.25 MHz for FPGA register access
    spi_set_baudrate(EXT_SPI, 3250000);

    // Write direction register
    gpio_put(PIN_EXT_SPI_CS, 0);
    spi_write_blocking(EXT_SPI, &cmd_wr, 1);
    spi_write_blocking(EXT_SPI, dir_data, 2);
    gpio_put(PIN_EXT_SPI_CS, 1);

    sleep_us(10);

    // Readback and verify
    uint8_t readback[2] = { 0xFF, 0xFF };
    gpio_put(PIN_EXT_SPI_CS, 0);
    spi_write_blocking(EXT_SPI, &cmd_rd, 1);
    spi_read_blocking(EXT_SPI, 0x00, readback, 2);
    gpio_put(PIN_EXT_SPI_CS, 1);

    // Restore SPI rate
    spi_set_baudrate(EXT_SPI, 5000000);

    bool ok = (readback[0] == dir_data[0] && readback[1] == dir_data[1]);
    printf("[FPGA] io_dir: want=%02X%02X got=%02X%02X → %s\n",
           dir_data[0], dir_data[1], readback[0], readback[1],
           ok ? "VERIFIED" : "MISMATCH");
    return ok;
}

/* ──────────────────────────────────────────────────────────
 * Init: External buses
 * ────────────────────────────────────────────────────────── */
static void init_external_buses(void) {
    spi_init(EXT_SPI, 5000000);
    gpio_set_function(PIN_EXT_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_SPI_CS, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_EXT_SPI_MOSI, GPIO_FUNC_SPI);

    i2c_init(EXT_I2C, EXT_I2C_FREQ_DEFAULT);
    gpio_set_function(PIN_EXT_I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(PIN_EXT_I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(PIN_EXT_I2C_SDA);
    gpio_pull_up(PIN_EXT_I2C_SCL);

    gpio_init(PIN_EXT_GPIO_24);
    gpio_set_dir(PIN_EXT_GPIO_24, GPIO_OUT);
    gpio_init(PIN_EXT_GPIO_25);
    gpio_set_dir(PIN_EXT_GPIO_25, GPIO_OUT);
    gpio_init(PIN_EXT_GPIO_26);
    gpio_set_dir(PIN_EXT_GPIO_26, GPIO_IN);
}

/* ──────────────────────────────────────────────────────────
 * Probe Orca on UART1 — tries multiple baud/flow combos
 * ────────────────────────────────────────────────────────── */
static uint32_t orca_active_baud = 0;

static bool probe_orca(void) {
    static ipp_rx_ctx_t orca_rx;

    struct { uint32_t baud; bool hwflow; const char *label; } attempts[] = {
        { EXT_UART_BAUD_DEFAULT, false, "115200 no-flow" },
        { ORCA_BAUD,             false, "921600 no-flow" },
        { ORCA_BAUD,             true,  "921600 hw-flow" },
    };

    // PCA9555 port 0 = 0xDA: level shifter DIR for UART (TX/RTS = output, RX/CTS = input)
    uint8_t ioexp_p0[2] = { 0x02, 0xDA };
    ipp_send_display(IPP_MSG_IOEXP_WRITE, ioexp_p0, 2);
    sleep_ms(20);
    // PCA9555 port 1 = 0xB8: I2C pullup, GPIO25, antenna 400MHz paths
    uint8_t ioexp_p1[2] = { 0x03, 0xB8 };
    ipp_send_display(IPP_MSG_IOEXP_WRITE, ioexp_p1, 2);
    sleep_ms(50);

    for (int i = 0; i < 3; i++) {
        uart_deinit(EXT_UART);
        uart_init(EXT_UART, attempts[i].baud);
        gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
        gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);

        if (attempts[i].hwflow) {
            gpio_set_function(PIN_EXT_UART_CTS, GPIO_FUNC_UART);
            gpio_set_function(PIN_EXT_UART_RTS, GPIO_FUNC_UART);
            uart_set_hw_flow(EXT_UART, true, true);
        } else {
            uart_set_hw_flow(EXT_UART, false, false);
        }

        uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
        uart_set_fifo_enabled(EXT_UART, true);
        while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);

        printf("  [%s] TX ping...", attempts[i].label);
        ipp_send_orca(IPP_MSG_PING, NULL, 0);

        absolute_time_t deadline = make_timeout_time_ms(500);
        bool got_pong = false;
        int rx_bytes = 0;
        uint8_t raw[32];

        ipp_rx_init(&orca_rx, NULL, NULL);

        while (!time_reached(deadline)) {
            if (uart_is_readable(EXT_UART)) {
                uint8_t byte = uart_getc(EXT_UART);
                if (rx_bytes < (int)sizeof(raw)) raw[rx_bytes] = byte;
                rx_bytes++;
                ipp_rx_feed(&orca_rx, byte);
                if (orca_rx.frames_ok > 0) { got_pong = true; break; }
            }
        }

        printf(" RX=%d", rx_bytes);
        if (rx_bytes > 0 && !got_pong) {
            printf(" [");
            int show = rx_bytes < 16 ? rx_bytes : 16;
            for (int j = 0; j < show; j++) printf("%02X ", raw[j]);
            if (rx_bytes > 16) printf("...");
            printf("]");
        }
        printf(" %s\n", got_pong ? "PONG!" : "timeout");

        if (got_pong) {
            orca_active_baud = attempts[i].baud;
            return true;
        }
    }

    uart_deinit(EXT_UART);
    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
    orca_active_baud = 0;

    return false;
}

/* ──────────────────────────────────────────────────────────
 * Host Serial (USB CDC)
 * ────────────────────────────────────────────────────────── */
static void init_host_serial(void) {
    stdio_usb_init();
}

/* ──────────────────────────────────────────────────────────
 * Safe Mode Check
 * ────────────────────────────────────────────────────────── */
static bool check_safe_mode(void) {
    sleep_ms(100);

    while (btn_evt_tail != btn_evt_head) {
        btn_evt_t evt;
        evt.id = btn_evts[btn_evt_tail].id;
        evt.state = btn_evts[btn_evt_tail].state;
        btn_evt_tail = (btn_evt_tail + 1) % BTN_EVT_SIZE;

        if (evt.id == IPP_BTN_RED && evt.state == BTN_STATE_PRESSED) {
            return true;
        }
    }
    return false;
}

/* ──────────────────────────────────────────────────────────
 * Tool Registration
 * ────────────────────────────────────────────────────────── */
static void register_tools(void) {
    tool_register(&tool_subghz_desc);
    tool_register(&tool_ghost_desc);
    tool_register(&tool_apps_desc);
    tool_register(&tool_bus_desc);
    tool_register(&tool_gpio_desc);
    tool_register(&tool_ir_desc);
    tool_register(&tool_settings_desc);
    tool_register(&tool_about_desc);
}

/* ──────────────────────────────────────────────────────────
 * Send Main Menu to Display
 *
 * Builds the menu payload from the tool registry.
 * Items with unavailable hardware are marked disabled.
 * ────────────────────────────────────────────────────────── */
static void send_main_menu(void) {
    uint8_t payload[512];
    size_t pos = 0;
    int count = tool_count();

    payload[pos++] = menu_selected;
    payload[pos++] = (uint8_t)count;

    for (int i = 0; i < count; i++) {
        const tool_desc_t *t = tool_get(i);
        bool available = tool_hw_available(i);

        payload[pos++] = t->icon_id;
        payload[pos++] = available ? 0 : 0x01;

        size_t nlen = strlen(t->name) + 1;
        if (pos + nlen > sizeof(payload)) break;
        memcpy(&payload[pos], t->name, nlen);
        pos += nlen;
    }

    ipp_send_display(IPP_MSG_MENU_SHOW, payload, (uint16_t)pos);
}

/* ──────────────────────────────────────────────────────────
 * Return to Main Menu
 *
 * Called when the active tool exits. Restores status bar
 * and redraws the menu.
 * ────────────────────────────────────────────────────────── */
static void return_to_menu(void) {
    tool_send_status_bar("Home");
    send_main_menu();
}

/* ──────────────────────────────────────────────────────────
 * Core 1 Entry — Flash I/O Worker
 * ────────────────────────────────────────────────────────── */
static void core1_entry(void) {
    flash_safe_execute_core_init();
    while (true) {
        uint32_t cmd = multicore_fifo_pop_blocking();

        switch (cmd) {
        case 0x01:
            multicore_fifo_push_blocking(0x01);
            break;

        case 0x02:
            multicore_fifo_push_blocking(0x02);
            break;

        case 0x03:
            multicore_fifo_push_blocking(0x03);
            break;

        default:
            multicore_fifo_push_blocking(0xFF);
            break;
        }
    }
}

void serial_list_apps_cb(const char *name, uint32_t size, void *user) {
    int *count = (int *)user;
    printf("APP %s %lu\n", name, (unsigned long)size);
    (*count)++;
}

/* ──────────────────────────────────────────────────────────
 * Main Entry Point
 * ────────────────────────────────────────────────────────── */
int main(void) {
    // Phase 1: Hard reset Display
    boot_reset_display();

    // Phase 2: IPP link
    init_ipp_uart();

    absolute_time_t deadline = make_timeout_time_ms(3000);
    while (!display_ready && !time_reached(deadline)) {
        tight_loop_contents();
    }

    // Phase 3: Safe mode check
    safe_mode = check_safe_mode();

    if (safe_mode) {
        menu_selected = 6;
    }

    // Phase 4: Hardware init
    init_radio_spi();

    uint8_t r1_part, r1_ver, r2_part, r2_ver;
    radio1_present = probe_radio(PIN_RADIO1_CS, &r1_part, &r1_ver);
    radio2_present = probe_radio(PIN_RADIO2_CS, &r2_part, &r2_ver);

    init_fpga();
    init_external_buses();
    fpga_write_io_dir();

    if (!safe_mode) {
        printf("  Waiting for Orca boot...\n");
        sleep_ms(1000);
        orca_present = probe_orca();
        // Always enable UART1 IRQ — needed for GhostESP raw text mode
        // even when IPP probe fails
        init_orca_irq();
    }

    // Phase 5: Tool engine
    tool_engine_init(ipp_send_display);
    tool_engine_set_orca_send(ipp_send_orca);

    tool_hw_status_t hw = {
        .radio1_ok = radio1_present,
        .radio2_ok = radio2_present,
        .fpga_ok   = fpga_configured,
        .orca_ok   = orca_present,
        .battery_pct = 0,
        .battery_charging = false,
    };
    tool_engine_set_hw(&hw);

    register_tools();
    settings_boot_apply();

    // Phase 6: Host serial
    init_host_serial();

    // Phase 7: Launch Core 1
    multicore_launch_core1(core1_entry);

    // Phase 7b: Mount filesystem (needs Core 1 for flash_safe_execute)
    fs_init();

    // Phase 8: Initial UI
    tool_send_status_bar("Home");
    send_main_menu();

    if (safe_mode) {
        tool_send_toast("SAFE MODE", 3000);
    }

    // Phase 9: Boot summary
    printf("\n");
    printf("=================================\n");
    printf("  Akhlut CFW v0.1.0\n");
    printf("=================================\n");
    printf("  Radio 1: %s", radio1_present ? "OK" : "NOT FOUND");
    if (radio1_present) printf(" (part=0x%02X ver=0x%02X)", r1_part, r1_ver);
    printf("\n");
    printf("  Radio 2: %s", radio2_present ? "OK" : "NOT FOUND");
    if (radio2_present) printf(" (part=0x%02X ver=0x%02X)", r2_part, r2_ver);
    printf("\n");
    printf("  FPGA:    %s\n", fpga_configured ? "CONFIGURED" : "NOT READY");
    printf("  Orca:    %s\n", orca_present ? "CONNECTED" : "NOT PRESENT");
    printf("  Mode:    %s\n", safe_mode ? "SAFE MODE" : "NORMAL");
    printf("  Display: %s\n", display_ready ? "READY" : "NO RESPONSE");
    printf("  Tools:   %d registered\n", tool_count());
    printf("=================================\n");
    printf("> ");

    last_activity_ms = to_ms_since_boot(get_absolute_time());

    // Main Loop (Core 0)
    while (true) {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // Drain button events
        while (btn_evt_tail != btn_evt_head) {
            last_activity_ms = now;

            if (display_asleep) {
                display_asleep = false;
                ipp_backlight_t bl;
                bl.brightness = settings_get_brightness();
                ipp_send_display(IPP_MSG_BACKLIGHT, &bl, sizeof(bl));
                btn_evt_tail = (btn_evt_tail + 1) % BTN_EVT_SIZE;
                continue;
            }

            btn_evt_t evt;
            evt.id = btn_evts[btn_evt_tail].id;
            evt.state = btn_evts[btn_evt_tail].state;
            btn_evt_tail = (btn_evt_tail + 1) % BTN_EVT_SIZE;

            const char *btn_names[] = {
                "GRAY", "YELLOW", "GREEN", "BLUE", "RED"
            };
            if (evt.id < 5 && evt.state == BTN_STATE_PRESSED) {
                printf("[Button] %s\n> ", btn_names[evt.id]);
            }

            if (tool_is_active()) {
                // Forward all events (PRESSED, RELEASED, HELD) to tool
                tool_on_button(evt.id, evt.state);

                if (!tool_is_active()) {
                    return_to_menu();
                }
            } else {
                // Main menu: only act on PRESSED
                if (evt.state != BTN_STATE_PRESSED) continue;

                int count = tool_count();

                switch (evt.id) {
                case IPP_BTN_YELLOW:
                    if (menu_selected > 0)
                        menu_selected--;
                    else
                        menu_selected = count - 1;
                    send_main_menu();
                    break;

                case IPP_BTN_BLUE:
                    if (menu_selected < count - 1)
                        menu_selected++;
                    else
                        menu_selected = 0;
                    send_main_menu();
                    break;

                case IPP_BTN_GREEN:
                    if (!tool_hw_available(menu_selected)) {
                        tool_send_toast("Hardware not available", 1500);
                    } else if (!tool_launch(menu_selected)) {
                        tool_send_toast("Coming soon", 1500);
                    } else {
                        ir_pending = false;
                        tool_send_status_bar(tool_get(menu_selected)->name);
                        printf("[Tool] Launched: %s\n> ",
                               tool_get(menu_selected)->name);
                    }
                    break;

                case IPP_BTN_RED:
                    break;

                default:
                    break;
                }
            }
        }

        // Drain Orca messages → active tool
        while (orca_pend_tail != orca_pend_head) {
            volatile orca_pending_msg_t *msg =
                &orca_pending[orca_pend_tail];
            tool_on_orca_msg(msg->type,
                             (const uint8_t *)msg->payload, msg->len);
            orca_pend_tail = (orca_pend_tail + 1) % ORCA_MSG_SLOTS;
        }

        // Drain IR received events → active tool
        if (ir_pending) {
            ir_pending = false;
            ipp_ir_code_t code;
            code.protocol = ir_pending_code.protocol;
            code.code     = ir_pending_code.code;
            code.bits     = ir_pending_code.bits;
            tool_on_display_msg(IPP_MSG_IR_RECEIVED,
                                (const uint8_t *)&code, sizeof(code));
        }

        if (i2c_scan_pending) {
            i2c_scan_pending = false;
            tool_on_display_msg(IPP_MSG_I2C_SCAN_RESP,
                                (const uint8_t *)&i2c_scan_resp_buf,
                                1 + i2c_scan_resp_buf.count);
        }

        if (ioexp_resp_pending) {
            ioexp_resp_pending = false;
            printf("[IOexp] reg=0x%02X val=0x%02X %s\n> ",
                   ioexp_resp_reg, ioexp_resp_val,
                   ioexp_resp_status ? "FAIL" : "OK");
        }

        // Update battery in tool engine when new data arrives
        if (battery_dirty) {
            battery_dirty = false;
            tool_hw_status_t hw = {
                .radio1_ok = radio1_present,
                .radio2_ok = radio2_present,
                .fpga_ok   = fpga_configured,
                .orca_ok   = orca_present,
                .battery_pct = battery_pct,
                .battery_charging = battery_charging,
            };
            tool_engine_set_hw(&hw);

            printf("[Battery] %umV vbus=%umV ichg=%umA pct=%u%% flags=0x%02X%s%s\n",
                   battery_vbatt_mv, battery_vbus_mv, battery_ichg_ma,
                   battery_pct, battery_flags_raw,
                   battery_charging ? " CHARGING" : "",
                   (battery_flags_raw & BATTERY_FLAG_VBUS_GD) ? " VBUS_GD" : "");

            const char *name = tool_is_active() ? tool_active_name() : "Home";
            tool_send_status_bar(name);
        }

        // Auto-sleep: dim screen after inactivity
        {
            uint8_t sleep_min = settings_get_sleep_min();
            if (sleep_min > 0 && !display_asleep) {
                uint32_t timeout_ms = (uint32_t)sleep_min * 60000u;
                if (now - last_activity_ms > timeout_ms) {
                    display_asleep = true;
                    ipp_backlight_t bl;
                    bl.brightness = 0;
                    ipp_send_display(IPP_MSG_BACKLIGHT, &bl, sizeof(bl));
                }
            }
        }

        // Update active tool
        if (tool_is_active()) {
            tool_update();

            if (!tool_is_active()) {
                return_to_menu();
            }
        }

        // Host serial (suppressed during UART passthrough)
        if (!tool_serial_passthru) {
            int ch = getchar_timeout_us(0);
            if (ch != PICO_ERROR_TIMEOUT) {
                if (ch == 'B') {
                    printf("\n[Rebooting Display to UF2 bootloader...]\n");
                    ipp_send_display(IPP_MSG_REBOOT_BOOTLOADER, NULL, 0);
                } else if (ch == 'R') {
                    printf("\n[HW reset Display — hold blue button NOW!]\n");
                    sleep_ms(2000);
                    printf("[Pulsing RUN pin...]\n");
                    gpio_init(PIN_DISPLAY_RUN);
                    gpio_set_dir(PIN_DISPLAY_RUN, GPIO_OUT);
                    gpio_put(PIN_DISPLAY_RUN, 0);
                    sleep_ms(100);
                    gpio_put(PIN_DISPLAY_RUN, 1);
                    printf("[Done — check for RPI-RP2 drive]\n> ");
                } else if (ch == 's') {
                    printf("\n--- Status ---\n");
                    printf("  FPGA:    %s (CDONE=%d)\n",
                           fpga_configured ? "CONFIGURED" : "NOT READY",
                           gpio_get(PIN_FPGA_CDONE));
                    printf("  Radio 1: %s\n", radio1_present ? "OK" : "N/A");
                    printf("  Radio 2: %s\n", radio2_present ? "OK" : "N/A");
                    printf("  Orca:    %s\n", orca_present ? "CONNECTED" : "N/A");
                    printf("  Tool:    %s\n",
                           tool_is_active() ? tool_active_name() : "(none)");
                    printf("  Battery: %umV %u%%\n",
                           battery_vbatt_mv, battery_pct);
                    printf("  GP8(TX)=%d GP9(RX)=%d GP10(CTS)=%d GP11(RTS)=%d\n",
                           gpio_get(8), gpio_get(9), gpio_get(10), gpio_get(11));
                    printf("--------------\n> ");
                } else if (ch == 'p') {
                    static uint8_t dir_idx = 0;
                    static const uint8_t dirs[] = {
                        0xFF, 0x00, 0xAA, 0x55,
                        0x0F, 0xF0, 0x33, 0xCC
                    };
                    uint8_t val = dirs[dir_idx];
                    printf("\n[IOexp] Setting port 0 output = 0x%02X\n> ",
                           val);
                    uint8_t payload[2] = { 0x02, val };
                    ipp_send_display(IPP_MSG_IOEXP_WRITE, payload, 2);
                    dir_idx = (dir_idx + 1) % sizeof(dirs);
                } else if (ch == 'o') {
                    printf("\n[Orca] Re-probing...\n");
                    orca_present = probe_orca();
                    printf("[Orca] %s\n> ",
                           orca_present ? "CONNECTED!" : "not found");
                } else if (ch == 'a') {
                    printf("\n[AT test] Sending AT commands on UART1...\n");
                    static const uint32_t bauds[] = {115200, 921600, 9600, 460800};
                    for (int bi = 0; bi < 4; bi++) {
                        uart_deinit(EXT_UART);
                        uart_init(EXT_UART, bauds[bi]);
                        gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                        gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                        uart_set_hw_flow(EXT_UART, false, false);
                        uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
                        uart_set_fifo_enabled(EXT_UART, true);
                        while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                        const char *at_cmd = "AT\r\n";
                        uart_write_blocking(EXT_UART, (const uint8_t *)at_cmd, 4);
                        sleep_ms(200);
                        int rx_count = 0;
                        printf("  %lu baud: ", bauds[bi]);
                        while (uart_is_readable(EXT_UART) && rx_count < 64) {
                            uint8_t b = uart_getc(EXT_UART);
                            printf("%02X ", b);
                            rx_count++;
                        }
                        printf("(%d bytes)\n", rx_count);
                        if (rx_count > 0) break;
                    }
                    uart_deinit(EXT_UART);
                    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
                    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    printf("> ");
                } else if (ch == 'r') {
                    printf("\n[RX monitor] Watching GP9 for 2 sec...\n");
                    // Switch RX to GPIO input to detect any toggling
                    gpio_init(PIN_EXT_UART_RX);
                    gpio_set_dir(PIN_EXT_UART_RX, GPIO_IN);
                    gpio_pull_up(PIN_EXT_UART_RX);
                    sleep_us(10);
                    int transitions = 0;
                    int last = gpio_get(PIN_EXT_UART_RX);
                    int samples = 0;
                    absolute_time_t end_time = make_timeout_time_ms(2000);
                    while (!time_reached(end_time)) {
                        int val = gpio_get(PIN_EXT_UART_RX);
                        if (val != last) {
                            transitions++;
                            last = val;
                        }
                        samples++;
                    }
                    printf("  %d transitions in %d samples (final=%d)\n",
                           transitions, samples, last);
                    // Restore UART function
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    printf("> ");
                } else if (ch == 'v') {
                    static uint8_t v_idx = 0;
                    static const uint8_t vals[] = {
                        0x01, 0x02, 0x04, 0x08,
                        0x10, 0x20, 0x40, 0x80,
                        0xFF, 0x00, 0xDF, 0x3F
                    };
                    uint8_t val = vals[v_idx];
                    printf("\n[IOexp] Port 1 output = 0x%02X\n> ", val);
                    uint8_t payload[2] = { 0x03, val };
                    ipp_send_display(IPP_MSG_IOEXP_WRITE, payload, 2);
                    v_idx = (v_idx + 1) % sizeof(vals);
                } else if (ch == 'V') {
                    printf("\n[IOexp] Setting port 1 config → all outputs\n> ");
                    uint8_t payload[2] = { 0x07, 0x00 };
                    ipp_send_display(IPP_MSG_IOEXP_WRITE, payload, 2);
                } else if (ch == 'P') {
                    printf("\n[IOexp] Reading all registers...\n");
                    for (uint8_t reg = 0; reg < 8; reg++) {
                        ipp_send_display(IPP_MSG_IOEXP_READ, &reg, 1);
                    }
                    printf("> ");
                } else if (ch == 'F') {
                    printf("\n[FPGA] Re-writing io_dir and reading back...\n");
                    fpga_write_io_dir();
                    printf("> ");
                } else if (ch == 'T') {
                    printf("\n[Passthrough] UART1 <-> USB binary @ 115200.\n");
                    printf("Triple-ESC (3x in 500ms) to exit.\n");
                    uart_deinit(EXT_UART);
                    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
                    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    uart_set_hw_flow(EXT_UART, false, false);
                    uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
                    uart_set_fifo_enabled(EXT_UART, true);
                    while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                    int esc_count = 0;
                    absolute_time_t esc_deadline = nil_time;
                    bool passthru_active = true;
                    while (passthru_active) {
                        if (uart_is_readable(EXT_UART)) {
                            uint8_t b = uart_getc(EXT_UART);
                            putchar_raw(b);
                        }
                        int uch = getchar_timeout_us(0);
                        if (uch != PICO_ERROR_TIMEOUT) {
                            if (uch == 0x1B) {
                                esc_count++;
                                if (esc_count == 1)
                                    esc_deadline = make_timeout_time_ms(500);
                                if (esc_count >= 3) { passthru_active = false; break; }
                            } else {
                                esc_count = 0;
                                uart_putc(EXT_UART, (uint8_t)uch);
                            }
                        }
                        if (esc_count > 0 && !is_nil_time(esc_deadline) && time_reached(esc_deadline))
                            esc_count = 0;
                    }
                    printf("\n[Passthrough] ended.\n> ");
                } else if (ch == 'd') {
                    printf("\n[Dump] Listening on UART1 @ 115200 for 3 sec...\n");
                    uart_deinit(EXT_UART);
                    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
                    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    uart_set_hw_flow(EXT_UART, false, false);
                    uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
                    uart_set_fifo_enabled(EXT_UART, true);
                    while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                    absolute_time_t end_t = make_timeout_time_ms(3000);
                    int cnt = 0;
                    while (!time_reached(end_t) && cnt < 256) {
                        if (uart_is_readable(EXT_UART)) {
                            uint8_t b = uart_getc(EXT_UART);
                            printf("%02X ", b);
                            cnt++;
                            if (cnt % 16 == 0) printf("\n");
                        }
                    }
                    printf("\n(%d bytes)\n> ", cnt);
                } else if (ch == 'D') {
                    printf("\n[Multi-baud dump] Listening at various rates...\n");
                    static const uint32_t try_bauds[] = {
                        9600, 19200, 38400, 57600, 74880,
                        115200, 128000, 153600, 230400, 256000,
                        460800, 500000, 750000, 921600, 1000000,
                        1500000, 2000000
                    };
                    for (int bi = 0; bi < 17; bi++) {
                        uart_deinit(EXT_UART);
                        uart_init(EXT_UART, try_bauds[bi]);
                        gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                        gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                        uart_set_hw_flow(EXT_UART, false, false);
                        uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
                        uart_set_fifo_enabled(EXT_UART, true);
                        while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                        sleep_ms(10);
                        while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                        absolute_time_t dl = make_timeout_time_ms(500);
                        uint8_t buf[64];
                        int cnt = 0;
                        while (!time_reached(dl) && cnt < 64) {
                            if (uart_is_readable(EXT_UART)) {
                                buf[cnt++] = uart_getc(EXT_UART);
                            }
                        }
                        printf("  %lu: %d bytes [", try_bauds[bi], cnt);
                        int show = cnt < 24 ? cnt : 24;
                        for (int j = 0; j < show; j++) printf("%02X ", buf[j]);
                        if (cnt > 24) printf("...");
                        printf("] ");
                        int printable = 0, sync_count = 0;
                        for (int j = 0; j < cnt; j++) {
                            if (buf[j] >= 0x20 && buf[j] < 0x7F) printable++;
                            if (buf[j] == 0xF1) sync_count++;
                        }
                        if (cnt > 0)
                            printf("(%d%% print", printable * 100 / cnt);
                        if (sync_count > 0)
                            printf(" F1x%d!", sync_count);
                        printf(")\n");
                    }
                    uart_deinit(EXT_UART);
                    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
                    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    printf("> ");
                } else if (ch == 'i') {
                    printf("\n[IPP ping] Sending IPP PING @ 115200...\n");
                    uart_deinit(EXT_UART);
                    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
                    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    uart_set_hw_flow(EXT_UART, false, false);
                    uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
                    uart_set_fifo_enabled(EXT_UART, true);
                    while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                    ipp_send_orca(IPP_MSG_PING, NULL, 0);
                    printf("  Sent. Waiting 1s...\n  ");
                    absolute_time_t dl = make_timeout_time_ms(1000);
                    int rxc = 0;
                    while (!time_reached(dl) && rxc < 128) {
                        if (uart_is_readable(EXT_UART)) {
                            uint8_t b = uart_getc(EXT_UART);
                            if (b >= 0x20 && b < 0x7F) printf("%c", b);
                            else printf("[%02X]", b);
                            rxc++;
                        }
                    }
                    printf("\n  (%d bytes)\n> ", rxc);
                } else if (ch == 'u') {
                    printf("\n[UART1 diag]\n");
                    // Read CTS pin directly as GPIO
                    gpio_init(PIN_EXT_UART_CTS);
                    gpio_set_dir(PIN_EXT_UART_CTS, GPIO_IN);
                    gpio_pull_up(PIN_EXT_UART_CTS);
                    sleep_us(10);
                    int cts_val = gpio_get(PIN_EXT_UART_CTS);
                    printf("  CTS(GP10) raw = %d (%s)\n", cts_val,
                           cts_val ? "HIGH/deasserted" : "LOW/asserted");
                    // Also read TX, RX, RTS as GPIO
                    gpio_init(PIN_EXT_UART_RX);
                    gpio_set_dir(PIN_EXT_UART_RX, GPIO_IN);
                    gpio_pull_up(PIN_EXT_UART_RX);
                    sleep_us(10);
                    printf("  RX (GP9)  raw = %d\n", gpio_get(PIN_EXT_UART_RX));
                    // Re-init UART without flow control, send test pattern
                    uart_deinit(EXT_UART);
                    uart_init(EXT_UART, EXT_UART_BAUD_DEFAULT);
                    gpio_set_function(PIN_EXT_UART_TX, GPIO_FUNC_UART);
                    gpio_set_function(PIN_EXT_UART_RX, GPIO_FUNC_UART);
                    uart_set_hw_flow(EXT_UART, false, false);
                    uart_set_format(EXT_UART, 8, 1, UART_PARITY_NONE);
                    uart_set_fifo_enabled(EXT_UART, true);
                    // Drain
                    while (uart_is_readable(EXT_UART)) uart_getc(EXT_UART);
                    // Send test bytes
                    const uint8_t test_pattern[] = { 0x55, 0xAA, 0x0F, 0xF0 };
                    printf("  TX 4 bytes @ 115200...");
                    uart_write_blocking(EXT_UART, test_pattern, 4);
                    // Wait and collect any echo
                    sleep_ms(100);
                    int rx_count = 0;
                    printf(" RX: ");
                    while (uart_is_readable(EXT_UART) && rx_count < 32) {
                        uint8_t b = uart_getc(EXT_UART);
                        printf("%02X ", b);
                        rx_count++;
                    }
                    printf("(%d bytes)\n> ", rx_count);
                } else if (ch == 'L') {
                    printf("\nLIST\n");
                    int app_count = 0;
                    extern void serial_list_apps_cb(const char *, uint32_t, void *);
                    fs_list("/apps", serial_list_apps_cb, &app_count);
                    printf("END %d\n> ", app_count);
                } else if (ch == 'X') {
                    printf("\nDELREADY\n");
                    #define DEL_TIMEOUT_US 2000000
                    int plen_ch = getchar_timeout_us(DEL_TIMEOUT_US);
                    if (plen_ch == PICO_ERROR_TIMEOUT) { printf("ERR timeout\n> "); }
                    else {
                        uint8_t plen = (uint8_t)plen_ch;
                        char path[64];
                        bool ok = true;
                        for (uint8_t pi = 0; pi < plen && pi < 63; pi++) {
                            int c = getchar_timeout_us(DEL_TIMEOUT_US);
                            if (c == PICO_ERROR_TIMEOUT) { ok = false; break; }
                            path[pi] = (char)c;
                        }
                        path[plen < 63 ? plen : 63] = '\0';
                        if (!ok) { printf("ERR timeout\n> "); }
                        else {
                            int err = fs_delete(path);
                            if (err == 0) printf("OK %s\n> ", path);
                            else printf("ERR delete %d\n> ", err);
                        }
                    }
                    #undef DEL_TIMEOUT_US
                } else if (ch == 'U') {
                    printf("\nREADY\n");
                    // File upload: 1-byte path_len, path, 4-byte LE size, data
                    static uint8_t upload_buf[32 * 1024];
                    #define UPLOAD_TIMEOUT_US 2000000
                    int plen_ch = getchar_timeout_us(UPLOAD_TIMEOUT_US);
                    if (plen_ch == PICO_ERROR_TIMEOUT) { printf("ERR timeout\n> "); }
                    else {
                        uint8_t plen = (uint8_t)plen_ch;
                        char path[64];
                        bool ok = true;
                        for (uint8_t pi = 0; pi < plen && pi < 63; pi++) {
                            int c = getchar_timeout_us(UPLOAD_TIMEOUT_US);
                            if (c == PICO_ERROR_TIMEOUT) { ok = false; break; }
                            path[pi] = (char)c;
                        }
                        path[plen < 63 ? plen : 63] = '\0';
                        if (!ok) { printf("ERR timeout path\n> "); }
                        else {
                            uint32_t fsize = 0;
                            for (int si = 0; si < 4; si++) {
                                int c = getchar_timeout_us(UPLOAD_TIMEOUT_US);
                                if (c == PICO_ERROR_TIMEOUT) { ok = false; break; }
                                fsize |= ((uint32_t)(uint8_t)c) << (si * 8);
                            }
                            if (!ok) { printf("ERR timeout size\n> "); }
                            else if (fsize > sizeof(upload_buf)) {
                                printf("ERR too large %lu\n> ", fsize);
                            } else {
                                for (uint32_t bi = 0; bi < fsize; bi++) {
                                    int c = getchar_timeout_us(UPLOAD_TIMEOUT_US);
                                    if (c == PICO_ERROR_TIMEOUT) { ok = false; break; }
                                    upload_buf[bi] = (uint8_t)c;
                                }
                                if (!ok) { printf("ERR timeout data\n> "); }
                                else {
                                    fs_mkdir("/apps");
                                    int err = fs_write(path, upload_buf, fsize);
                                    if (err == 0)
                                        printf("OK %lu\n> ", fsize);
                                    else
                                        printf("ERR write %d\n> ", err);
                                }
                            }
                        }
                    }
                    #undef UPLOAD_TIMEOUT_US
                } else {
                    putchar(ch);
                    if (ch == '\r' || ch == '\n') {
                        printf("\n> ");
                    }
                }
            }
        }

        tight_loop_contents();
    }

    return 0;
}
