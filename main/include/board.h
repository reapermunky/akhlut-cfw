/**
 * board.h — FreeWili 1 Main RP2040 Pin Definitions
 *
 * Akhlut CFW
 * Source: OpenKeiko reverse engineering + vendor docs
 *
 * Every GPIO on the Main RP2040 is defined here. This is the
 * single source of truth for pin assignments. No magic numbers
 * anywhere else in the codebase.
 */

#ifndef BOARD_H
#define BOARD_H

/* ──────────────────────────────────────────────────────────
 * UART0 — Inter-Processor Link (Main ↔ Display)
 * 115200 baud, 8N1, hardware flow control
 * ────────────────────────────────────────────────────────── */
#define PIN_IPP_TX          0   // UART0 TX → Display RX
#define PIN_IPP_RX          1   // UART0 RX ← Display TX
#define PIN_IPP_CTS         2   // UART0 CTS ← Display RTS
#define PIN_IPP_RTS         3   // UART0 RTS → Display CTS
#define IPP_UART            uart0
#define IPP_UART_IRQ        UART0_IRQ
#define IPP_BAUD            115200

/* ──────────────────────────────────────────────────────────
 * SPI0 — Shared CC1101 Radio Bus
 * Both radios share MISO/MOSI/SCK, separate CS + GDO pins
 * ────────────────────────────────────────────────────────── */
#define PIN_RADIO_MISO      4   // SPI0 RX  — shared
#define PIN_RADIO_SCK       6   // SPI0 SCK — shared
#define PIN_RADIO_MOSI      7   // SPI0 TX  — shared
#define PIN_RADIO1_CS       18  // Radio 1 chip select
#define PIN_RADIO2_CS       5   // Radio 2 chip select (SPI0 CS repurposed)
#define PIN_RADIO1_GDO0     21  // Radio 1 GDO0 (packet RX/TX interrupt)
#define PIN_RADIO1_GDO2     19  // Radio 1 GDO2 (carrier sense / sync)
#define PIN_RADIO2_GDO0     20  // Radio 2 GDO0
#define PIN_RADIO2_GDO2     22  // Radio 2 GDO2
#define RADIO_SPI           spi0

/* ──────────────────────────────────────────────────────────
 * UART1 — External Header (also Orca link when attached)
 * Maps to GPIO header pins: TX→9, RX→5, CTS→7, RTS→11
 * ────────────────────────────────────────────────────────── */
#define PIN_EXT_UART_TX     8   // UART1 TX  → Header pin 9
#define PIN_EXT_UART_RX     9   // UART1 RX  ← Header pin 5
#define PIN_EXT_UART_CTS    10  // UART1 CTS ← Header pin 7
#define PIN_EXT_UART_RTS    11  // UART1 RTS → Header pin 11
#define EXT_UART            uart1
#define EXT_UART_IRQ        UART1_IRQ
#define EXT_UART_BAUD_DEFAULT 115200
#define ORCA_BAUD           921600

/* ──────────────────────────────────────────────────────────
 * SPI1 — External Header + FPGA Configuration
 * Maps to header pins: MISO→12, CS→1, SCK→15, MOSI→13
 * Also used for FPGA SPI slave config interface
 * ────────────────────────────────────────────────────────── */
#define PIN_EXT_SPI_MISO    12  // SPI1 RX   → Header pin 12
#define PIN_EXT_SPI_CS      13  // SPI1 CS   → Header pin 1
#define PIN_EXT_SPI_SCK     14  // SPI1 SCK  → Header pin 15
#define PIN_EXT_SPI_MOSI    15  // SPI1 TX   → Header pin 13
#define EXT_SPI             spi1

/* ──────────────────────────────────────────────────────────
 * I2C0 — External Header
 * Maps to header pins: SDA→10, SCL→8
 * PCA9517 level-shifted, 10K pull-ups (SW controlled)
 * ────────────────────────────────────────────────────────── */
#define PIN_EXT_I2C_SDA     16  // I2C0 SDA → Header pin 10
#define PIN_EXT_I2C_SCL     17  // I2C0 SCL → Header pin 8
#define EXT_I2C             i2c0
#define EXT_I2C_FREQ_DEFAULT 100000  // 100 kHz standard mode

/* ──────────────────────────────────────────────────────────
 * FPGA Control (iCE40UP5K)
 * ────────────────────────────────────────────────────────── */
#define PIN_FPGA_CLK        23  // 31.25 MHz clock output to FPGA
#define PIN_FPGA_CDONE      24  // FPGA config done (input, high = configured)
#define PIN_FPGA_CRESET     29  // FPGA config reset (output, active low)

/* ──────────────────────────────────────────────────────────
 * General Purpose GPIO — External Header
 * These route through sn74lxc1t45 level shifters via the FPGA
 * ────────────────────────────────────────────────────────── */
#define PIN_EXT_GPIO_24     27  // GP27 → Header pin 3  (output)
#define PIN_EXT_GPIO_25     25  // GP25 → Header pin 17 (output / status LED)
#define PIN_EXT_GPIO_26     26  // GP26 → Header pin 14 (input)

/* ──────────────────────────────────────────────────────────
 * Display RP2040 Control
 * ────────────────────────────────────────────────────────── */
#define PIN_DISPLAY_RUN     28  // Controls Display RP2040 RUN pin (active low reset)

/* ──────────────────────────────────────────────────────────
 * External Header Pin ↔ GPIO Cross-Reference
 *
 *  Header Pin | Function       | Main GPIO
 *  -----------|----------------|----------
 *  1          | SPI1 CS        | GP13
 *  2          | 5V out         | —
 *  3          | GPIO           | GP27
 *  4          | V_REF IN       | — (REQUIRED for GPIO)
 *  5          | UART1 RX       | GP9
 *  6          | 3.3V out       | —
 *  7          | UART1 CTS      | GP10
 *  8          | I2C0 SCL       | GP17
 *  9          | UART1 TX       | GP8
 *  10         | I2C0 SDA       | GP16
 *  11         | UART1 RTS      | GP11
 *  12         | SPI1 MISO      | GP12
 *  13         | SPI1 MOSI      | GP15
 *  14         | GPIO (input)   | GP26
 *  15         | SPI1 SCK       | GP14
 *  16         | SWCLK          | SWCLK (DIRECT — no level shifter!)
 *  17         | GPIO (output)  | GP25
 *  18         | SWDIO          | SWDIO (DIRECT — no level shifter!)
 *  19         | GND            | —
 *  20         | GND            | —
 * ────────────────────────────────────────────────────────── */

/* ──────────────────────────────────────────────────────────
 * Hardware Requirements Bitmask (for Tool.requires)
 * ────────────────────────────────────────────────────────── */
#define REQUIRE_RADIO1          (1 << 0)
#define REQUIRE_RADIO2          (1 << 1)
#define REQUIRE_RADIO_ANY       (1 << 2)
#define REQUIRE_FPGA            (1 << 3)
#define REQUIRE_ORCA            (1 << 4)
#define REQUIRE_HEADER_POWER    (1 << 5)  // Pin 4 must have voltage
#define REQUIRE_EXT_SPI         (1 << 6)
#define REQUIRE_EXT_I2C         (1 << 7)
#define REQUIRE_EXT_UART        (1 << 8)

/* ──────────────────────────────────────────────────────────
 * Button State Constants (shared with Display via IPP)
 * ────────────────────────────────────────────────────────── */
#define BTN_STATE_RELEASED  0
#define BTN_STATE_PRESSED   1
#define BTN_STATE_HELD      2

#endif // BOARD_H
