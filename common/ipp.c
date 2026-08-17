/**
 * ipp.c — Inter-Processor Protocol Implementation
 *
 * Akhlut CFW
 */

#include "ipp.h"
#include "pico/platform.h"

/* ──────────────────────────────────────────────────────────
 * CRC-16/CCITT
 * Polynomial: 0x1021, Init: 0xFFFF
 * Computed over: TYPE + LENGTH(2) + PAYLOAD
 * ────────────────────────────────────────────────────────── */
uint16_t __not_in_flash_func(ipp_crc16_update)(uint16_t crc, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint16_t ipp_crc16(const uint8_t *data, size_t len) {
    return ipp_crc16_update(0xFFFF, data, len);
}

/* ──────────────────────────────────────────────────────────
 * Frame Encoding
 * ────────────────────────────────────────────────────────── */
size_t ipp_encode(const ipp_frame_t *frame, uint8_t *out_buf, size_t buf_size) {
    size_t total = IPP_FRAME_OVERHEAD + frame->length;
    if (total > buf_size || frame->length > IPP_MAX_PAYLOAD) {
        return 0;
    }

    size_t pos = 0;

    // Header
    out_buf[pos++] = IPP_SYNC_BYTE;
    out_buf[pos++] = frame->seq;
    out_buf[pos++] = frame->type;
    out_buf[pos++] = (uint8_t)(frame->length & 0xFF);        // Length low
    out_buf[pos++] = (uint8_t)((frame->length >> 8) & 0xFF); // Length high

    // Payload
    if (frame->length > 0) {
        memcpy(&out_buf[pos], frame->payload, frame->length);
        pos += frame->length;
    }

    // CRC over TYPE + LENGTH + PAYLOAD
    // That's from out_buf[2] to out_buf[2 + 1 + 2 + length - 1]
    size_t crc_start = 2;  // Skip SYNC and SEQ
    size_t crc_len = 1 + 2 + frame->length;  // TYPE + LENGTH(2) + PAYLOAD
    uint16_t crc = ipp_crc16(&out_buf[crc_start], crc_len);

    out_buf[pos++] = (uint8_t)(crc & 0xFF);
    out_buf[pos++] = (uint8_t)((crc >> 8) & 0xFF);

    return pos;
}

/* ──────────────────────────────────────────────────────────
 * Convenience Builder
 * ────────────────────────────────────────────────────────── */
size_t ipp_build(uint8_t seq, uint8_t type,
                 const void *payload, uint16_t payload_len,
                 uint8_t *out_buf, size_t buf_size) {
    static ipp_frame_t frame;
    frame.seq = seq;
    frame.type = type;
    frame.length = payload_len;

    if (payload_len > 0 && payload != NULL) {
        if (payload_len > IPP_MAX_PAYLOAD) return 0;
        memcpy(frame.payload, payload, payload_len);
    }

    return ipp_encode(&frame, out_buf, buf_size);
}

/* ──────────────────────────────────────────────────────────
 * Receiver State Machine
 * ────────────────────────────────────────────────────────── */
void ipp_rx_init(ipp_rx_ctx_t *ctx, ipp_rx_callback_t cb, void *user_data) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state = IPP_RX_WAIT_SYNC;
    ctx->callback = cb;
    ctx->user_data = user_data;
}

void ipp_rx_reset(ipp_rx_ctx_t *ctx) {
    ctx->state = IPP_RX_WAIT_SYNC;
    ctx->payload_idx = 0;
}

/*
 * Feed one byte at a time. This is the hot path — called from ISR.
 *
 * On RP2040, decorate the caller (the UART IRQ handler) with
 * __not_in_flash_func() so it runs from SRAM during flash writes.
 * This function itself should also be placed in RAM:
 *   void __not_in_flash_func(ipp_rx_feed)(...) { ... }
 * (When compiling, add to CMake: pico_set_binary_type(target no_flash)
 *  or use __not_in_flash("ipp") section attribute.)
 */
void __not_in_flash_func(ipp_rx_feed)(ipp_rx_ctx_t *ctx, uint8_t byte) {
    switch (ctx->state) {

    case IPP_RX_WAIT_SYNC:
        if (byte == IPP_SYNC_BYTE) {
            ctx->state = IPP_RX_READ_SEQ;
        }
        // Else: discard — hunting for sync
        break;

    case IPP_RX_READ_SEQ:
        ctx->frame.seq = byte;
        ctx->state = IPP_RX_READ_TYPE;
        break;

    case IPP_RX_READ_TYPE:
        ctx->frame.type = byte;
        ctx->state = IPP_RX_READ_LEN_LO;
        break;

    case IPP_RX_READ_LEN_LO:
        ctx->frame.length = byte;  // Low byte
        ctx->state = IPP_RX_READ_LEN_HI;
        break;

    case IPP_RX_READ_LEN_HI:
        ctx->frame.length |= ((uint16_t)byte << 8);  // High byte
        if (ctx->frame.length > IPP_MAX_PAYLOAD) {
            // Frame too large — reject and resync
            ctx->frames_overflow++;
            ctx->state = IPP_RX_WAIT_SYNC;
        } else if (ctx->frame.length == 0) {
            // No payload — go straight to CRC
            ctx->state = IPP_RX_READ_CRC_LO;
        } else {
            ctx->payload_idx = 0;
            ctx->state = IPP_RX_READ_PAYLOAD;
        }
        break;

    case IPP_RX_READ_PAYLOAD:
        ctx->frame.payload[ctx->payload_idx++] = byte;
        if (ctx->payload_idx >= ctx->frame.length) {
            ctx->state = IPP_RX_READ_CRC_LO;
        }
        break;

    case IPP_RX_READ_CRC_LO:
        ctx->crc_lo = byte;
        ctx->state = IPP_RX_READ_CRC_HI;
        break;

    case IPP_RX_READ_CRC_HI: {
        ctx->frame.crc = ctx->crc_lo | ((uint16_t)byte << 8);

        uint8_t hdr[3] = {
            ctx->frame.type,
            (uint8_t)(ctx->frame.length & 0xFF),
            (uint8_t)((ctx->frame.length >> 8) & 0xFF)
        };
        uint16_t calc_crc = ipp_crc16_update(0xFFFF, hdr, 3);
        if (ctx->frame.length > 0) {
            calc_crc = ipp_crc16_update(calc_crc, ctx->frame.payload,
                                         ctx->frame.length);
        }

        if (calc_crc == ctx->frame.crc) {
            ctx->frames_ok++;
            if (ctx->callback) {
                ctx->callback(&ctx->frame, ctx->user_data);
            }
        } else {
            ctx->frames_crc_err++;
        }

        // Reset for next frame
        ctx->state = IPP_RX_WAIT_SYNC;
        ctx->payload_idx = 0;
        break;
    }

    default:
        ctx->state = IPP_RX_WAIT_SYNC;
        break;
    }
}

void ipp_rx_feed_buf(ipp_rx_ctx_t *ctx, const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ipp_rx_feed(ctx, buf[i]);
    }
}
