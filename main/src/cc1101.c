/**
 * cc1101.c — CC1101 Sub-GHz Radio Driver
 *
 * Akhlut CFW
 *
 * SPI0 is shared between two CC1101 radios. CS pin selects
 * which radio. Bus is initialized by init_radio_spi() in main.c.
 */

#include "cc1101.h"
#include "board.h"
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

void cc1101_reset(uint8_t cs) {
    gpio_put(cs, 0);
    sleep_us(10);
    gpio_put(cs, 1);
    sleep_us(40);

    gpio_put(cs, 0);
    sleep_us(1);
    uint8_t cmd = CC1101_CMD_SRES;
    spi_write_blocking(RADIO_SPI, &cmd, 1);
    gpio_put(cs, 1);
    sleep_ms(1);
}

void cc1101_strobe(uint8_t cs, uint8_t cmd) {
    gpio_put(cs, 0);
    sleep_us(1);
    spi_write_blocking(RADIO_SPI, &cmd, 1);
    gpio_put(cs, 1);
}

void cc1101_write_reg(uint8_t cs, uint8_t addr, uint8_t val) {
    uint8_t tx[2] = { addr & 0x3F, val };
    gpio_put(cs, 0);
    sleep_us(1);
    spi_write_blocking(RADIO_SPI, tx, 2);
    gpio_put(cs, 1);
}

uint8_t cc1101_read_reg(uint8_t cs, uint8_t addr) {
    uint8_t tx[2] = { (addr & 0x3F) | 0x80, 0x00 };
    uint8_t rx[2];
    gpio_put(cs, 0);
    sleep_us(1);
    spi_write_read_blocking(RADIO_SPI, tx, rx, 2);
    gpio_put(cs, 1);
    return rx[1];
}

uint8_t cc1101_read_status(uint8_t cs, uint8_t addr) {
    uint8_t tx[2] = { (addr & 0x3F) | 0xC0, 0x00 };
    uint8_t rx[2];
    gpio_put(cs, 0);
    sleep_us(1);
    spi_write_read_blocking(RADIO_SPI, tx, rx, 2);
    gpio_put(cs, 1);
    return rx[1];
}

void cc1101_write_config(uint8_t cs, const uint8_t config[][2], uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        cc1101_write_reg(cs, config[i][0], config[i][1]);
    }
}

void cc1101_set_freq(uint8_t cs, uint32_t freq_hz) {
    uint32_t freq_word = (uint32_t)((uint64_t)freq_hz * 65536 / CC1101_XOSC_FREQ);
    cc1101_write_reg(cs, CC1101_FREQ2, (freq_word >> 16) & 0xFF);
    cc1101_write_reg(cs, CC1101_FREQ1, (freq_word >> 8) & 0xFF);
    cc1101_write_reg(cs, CC1101_FREQ0, freq_word & 0xFF);
}

int8_t cc1101_read_rssi_dbm(uint8_t cs) {
    uint8_t raw = cc1101_read_status(cs, CC1101_STATUS_RSSI);
    int16_t rssi;
    if (raw >= 128)
        rssi = (int16_t)raw - 256;
    else
        rssi = (int16_t)raw;
    return (int8_t)(rssi / 2 - 74);
}

void cc1101_idle(uint8_t cs) {
    cc1101_strobe(cs, CC1101_CMD_SIDLE);
}

void cc1101_rx(uint8_t cs) {
    cc1101_strobe(cs, CC1101_CMD_SRX);
}

void cc1101_flush_tx(uint8_t cs) {
    cc1101_strobe(cs, CC1101_CMD_SFTX);
}

void cc1101_write_patable(uint8_t cs, const uint8_t *table, uint8_t len) {
    gpio_put(cs, 0);
    sleep_us(1);
    uint8_t hdr = CC1101_PATABLE | 0x40;
    spi_write_blocking(RADIO_SPI, &hdr, 1);
    spi_write_blocking(RADIO_SPI, table, len);
    gpio_put(cs, 1);
}

void cc1101_tx(uint8_t cs, const uint8_t *data, uint16_t len) {
    cc1101_idle(cs);
    cc1101_flush_tx(cs);

    uint16_t initial = (len > 64) ? 64 : len;
    gpio_put(cs, 0);
    sleep_us(1);
    uint8_t hdr = CC1101_TXFIFO | 0x40;
    spi_write_blocking(RADIO_SPI, &hdr, 1);
    spi_write_blocking(RADIO_SPI, data, initial);
    gpio_put(cs, 1);

    cc1101_strobe(cs, CC1101_CMD_STX);

    uint16_t sent = initial;
    while (sent < len) {
        uint8_t in_fifo = cc1101_read_status(cs, CC1101_STATUS_TXBYTES) & 0x7F;
        if (in_fifo < 32) {
            uint16_t space = 64 - in_fifo;
            uint16_t chunk = len - sent;
            if (chunk > space) chunk = space;
            gpio_put(cs, 0);
            sleep_us(1);
            hdr = CC1101_TXFIFO | 0x40;
            spi_write_blocking(RADIO_SPI, &hdr, 1);
            spi_write_blocking(RADIO_SPI, data + sent, chunk);
            gpio_put(cs, 1);
            sent += chunk;
        }
    }

    while ((cc1101_read_status(cs, CC1101_STATUS_TXBYTES) & 0x7F) > 0)
        sleep_us(100);

    sleep_us(1000);
    cc1101_idle(cs);
}

void cc1101_flush_rx(uint8_t cs) {
    cc1101_strobe(cs, CC1101_CMD_SFRX);
}

int cc1101_rx_read(uint8_t cs, uint8_t *buf, uint8_t max_len) {
    uint8_t rxbytes = cc1101_read_status(cs, CC1101_STATUS_RXBYTES) & 0x7F;
    if (rxbytes == 0) return 0;
    if (rxbytes > max_len) rxbytes = max_len;

    gpio_put(cs, 0);
    sleep_us(1);
    uint8_t hdr = CC1101_RXFIFO | 0xC0;
    spi_write_blocking(RADIO_SPI, &hdr, 1);
    spi_read_blocking(RADIO_SPI, 0x00, buf, rxbytes);
    gpio_put(cs, 1);

    cc1101_flush_rx(cs);
    cc1101_rx(cs);

    return (int)rxbytes;
}
