/**
 * cc1101.h — CC1101 Sub-GHz Radio Driver
 *
 * Akhlut CFW
 *
 * Minimal driver for the TI CC1101 transceiver on SPI0.
 * Two radios share the bus with separate CS pins.
 * All functions take a CS pin to select which radio.
 */

#ifndef CC1101_H
#define CC1101_H

#include <stdint.h>
#include <stdbool.h>

/* ──────────────────────────────────────────────────────────
 * Configuration Registers (0x00–0x2E)
 * ────────────────────────────────────────────────────────── */
#define CC1101_IOCFG2    0x00
#define CC1101_IOCFG1    0x01
#define CC1101_IOCFG0    0x02
#define CC1101_FIFOTHR   0x03
#define CC1101_PKTCTRL1  0x07
#define CC1101_PKTCTRL0  0x08
#define CC1101_FSCTRL1   0x0B
#define CC1101_FSCTRL0   0x0C
#define CC1101_FREQ2     0x0D
#define CC1101_FREQ1     0x0E
#define CC1101_FREQ0     0x0F
#define CC1101_MDMCFG4   0x10
#define CC1101_MDMCFG3   0x11
#define CC1101_MDMCFG2   0x12
#define CC1101_MDMCFG1   0x13
#define CC1101_MDMCFG0   0x14
#define CC1101_DEVIATN   0x15
#define CC1101_MCSM1     0x17
#define CC1101_MCSM0     0x18
#define CC1101_FOCCFG    0x19
#define CC1101_AGCCTRL2  0x1B
#define CC1101_AGCCTRL1  0x1C
#define CC1101_AGCCTRL0  0x1D
#define CC1101_FREND0    0x22
#define CC1101_PATABLE   0x3E
#define CC1101_TXFIFO    0x3F

/* ──────────────────────────────────────────────────────────
 * Command Strobes (single-byte write)
 * ────────────────────────────────────────────────────────── */
#define CC1101_CMD_SRES   0x30
#define CC1101_CMD_SCAL   0x33
#define CC1101_CMD_SRX    0x34
#define CC1101_CMD_STX    0x35
#define CC1101_CMD_SIDLE  0x36
#define CC1101_CMD_SFRX   0x3A
#define CC1101_CMD_SFTX   0x3B

/* ──────────────────────────────────────────────────────────
 * Status Registers (read with 0xC0 prefix)
 * ────────────────────────────────────────────────────────── */
#define CC1101_STATUS_PARTNUM    0x30
#define CC1101_STATUS_VERSION    0x31
#define CC1101_STATUS_RSSI       0x34
#define CC1101_STATUS_MARCSTATE  0x35
#define CC1101_STATUS_TXBYTES   0x3A
#define CC1101_STATUS_RXBYTES   0x3B
#define CC1101_RXFIFO           0x3F

/* MARCSTATE values */
#define MARCSTATE_IDLE    0x01
#define MARCSTATE_RX      0x0D
#define MARCSTATE_TX      0x13

/* Crystal oscillator frequency */
#define CC1101_XOSC_FREQ  26000000UL

/* ──────────────────────────────────────────────────────────
 * Driver API
 * ────────────────────────────────────────────────────────── */
void    cc1101_reset(uint8_t cs);
void    cc1101_strobe(uint8_t cs, uint8_t cmd);
void    cc1101_write_reg(uint8_t cs, uint8_t addr, uint8_t val);
uint8_t cc1101_read_reg(uint8_t cs, uint8_t addr);
uint8_t cc1101_read_status(uint8_t cs, uint8_t addr);
void    cc1101_write_config(uint8_t cs, const uint8_t config[][2], uint8_t count);
void    cc1101_set_freq(uint8_t cs, uint32_t freq_hz);
int8_t  cc1101_read_rssi_dbm(uint8_t cs);
void    cc1101_idle(uint8_t cs);
void    cc1101_rx(uint8_t cs);
void    cc1101_flush_tx(uint8_t cs);
void    cc1101_write_patable(uint8_t cs, const uint8_t *table, uint8_t len);
void    cc1101_tx(uint8_t cs, const uint8_t *data, uint16_t len);
void    cc1101_flush_rx(uint8_t cs);
int     cc1101_rx_read(uint8_t cs, uint8_t *buf, uint8_t max_len);

#endif
