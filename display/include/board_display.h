/**
 * board_display.h — FreeWili 1 Display RP2040 Pin Definitions
 *
 * Akhlut CFW
 * Source: OpenKeiko reverse engineering + vendor docs
 */

#ifndef BOARD_DISPLAY_H
#define BOARD_DISPLAY_H

/* ──────────────────────────────────────────────────────────
 * UART0 — Inter-Processor Link (Display ↔ Main)
 * 115200 baud, 8N1, hardware flow control
 * PCB traces are crossed (TX↔RX, CTS↔RTS)
 * ────────────────────────────────────────────────────────── */
#define PIN_IPP_TX          0   // UART0 TX → Main RX
#define PIN_IPP_RX          1   // UART0 RX ← Main TX
#define PIN_IPP_CTS         2   // UART0 CTS ← Main RTS
#define PIN_IPP_RTS         3   // UART0 RTS → Main CTS
#define IPP_UART            uart0
#define IPP_UART_IRQ        UART0_IRQ
#define IPP_BAUD            115200

/* ──────────────────────────────────────────────────────────
 * I2S Audio — Digital Speaker
 * ────────────────────────────────────────────────────────── */
#define PIN_I2S_DATA        4   // I2S serial data
#define PIN_I2S_BCLK        5   // I2S bit clock
#define PIN_I2S_WS          6   // I2S word select (LR clock)

/* ──────────────────────────────────────────────────────────
 * WS2812 RGB LED Chain
 * ────────────────────────────────────────────────────────── */
#define PIN_NEOPIXEL        7   // 7-element WS2812 chain
#define LED_COUNT           7

/* ──────────────────────────────────────────────────────────
 * Power Management
 * ────────────────────────────────────────────────────────── */
#define PIN_CHARGER_EN      8   // BQ25892 charger enable (active low, unconfirmed)

/* ──────────────────────────────────────────────────────────
 * IR Transmit / Receive
 * ────────────────────────────────────────────────────────── */
#define PIN_IR_TX           9   // IR transmitter output
#define PIN_IR_RX           16  // IR receiver input

/* ──────────────────────────────────────────────────────────
 * SPI1 — TFT Display (320×240 color LCD)
 * ────────────────────────────────────────────────────────── */
#define PIN_TFT_SCK         10  // SPI1 SCK
#define PIN_TFT_MOSI        11  // SPI1 TX (MOSI)
#define PIN_TFT_DC          12  // Data/Command select
#define PIN_TFT_CS          13  // SPI1 CS
#define PIN_TFT_BACKLIGHT   25  // Backlight PWM (0-255 brightness)
#define TFT_SPI             spi1
#define TFT_WIDTH           320
#define TFT_HEIGHT          240

/* ──────────────────────────────────────────────────────────
 * Buttons (active low unless noted)
 * ────────────────────────────────────────────────────────── */
#define PIN_BTN_GRAY        14  // Gray   — context action / power off
#define PIN_BTN_YELLOW      15  // Yellow — navigate up
#define PIN_BTN_GREEN       22  // Green  — select / confirm
#define PIN_BTN_BLUE        23  // Blue   — navigate down (also boot strap, active low)
#define PIN_BTN_RED         24  // Red    — back / cancel

// Button IDs (for IPP BUTTON_EVENT payload)
#define BTN_ID_GRAY         0
#define BTN_ID_YELLOW       1
#define BTN_ID_GREEN        2
#define BTN_ID_BLUE         3
#define BTN_ID_RED          4
#define BTN_COUNT           5

// Button event states
#define BTN_STATE_RELEASED  0
#define BTN_STATE_PRESSED   1
#define BTN_STATE_HELD      2   // Held > HOLD_THRESHOLD_MS

#define BTN_DEBOUNCE_MS     20
#define BTN_HOLD_MS         500
#define BTN_POWER_OFF_MS    3000  // Gray held for 3s = power off

/* ──────────────────────────────────────────────────────────
 * PDM Microphone
 * ────────────────────────────────────────────────────────── */
#define PIN_MIC_CLK         17  // PDM clock output
#define PIN_MIC_DATA        29  // PDM data input

/* ──────────────────────────────────────────────────────────
 * Unresolved GPIOs (OpenKeiko couldn't map these)
 * ────────────────────────────────────────────────────────── */
// GP18, GP19, GP20, GP21, GP28 — connections unknown

/* ──────────────────────────────────────────────────────────
 * I2C1 — Local Sensor / Control Bus
 * 400 kHz, onboard peripherals only
 * ────────────────────────────────────────────────────────── */
#define PIN_LOCAL_I2C_SDA   26  // I2C1 SDA
#define PIN_LOCAL_I2C_SCL   27  // I2C1 SCL
#define LOCAL_I2C           i2c1
#define LOCAL_I2C_FREQ      400000  // 400 kHz fast mode

// I2C device addresses
#define I2C_ADDR_ACCEL      0x19  // LIS3DH accelerometer
#define I2C_ADDR_IOEXP      0x21  // PCA9555/TCA9555 I/O expander
#define I2C_ADDR_CHARGER    0x6B  // BQ25892 charger / power-path
#define I2C_ADDR_RTC        0x6F  // MCP7940 real-time clock

/* ──────────────────────────────────────────────────────────
 * Display Color Palette (RGB565 for 16-bit TFT)
 * ────────────────────────────────────────────────────────── */
#define COLOR_BG            0x0841  // #0A0A0A near-black
#define COLOR_TEXT          0xE71C  // #E8E8E8 off-white
#define COLOR_HIGHLIGHT     0x0557  // #00AAFF electric blue
#define COLOR_ACTIVE        0x07F3  // #00FF66 signal green
#define COLOR_WARNING       0xFD40  // #FFAA00 amber
#define COLOR_ERROR         0xF9A6  // #FF3333 red
#define COLOR_MUTED         0x4208  // #444444 dark gray
#define COLOR_STATUS_BG     0x18C3  // #1A1A1A charcoal

/* ──────────────────────────────────────────────────────────
 * Screen Layout Constants
 * ────────────────────────────────────────────────────────── */
#define STATUS_BAR_HEIGHT   16
#define HINT_BAR_HEIGHT     16
#define CONTENT_Y           STATUS_BAR_HEIGHT
#define CONTENT_HEIGHT      (TFT_HEIGHT - STATUS_BAR_HEIGHT - HINT_BAR_HEIGHT)
#define CONTENT_WIDTH       TFT_WIDTH

#endif // BOARD_DISPLAY_H
