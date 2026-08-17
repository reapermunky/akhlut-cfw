/**
 * ipp.h — Inter-Processor Protocol Implementation
 *
 * Akhlut CFW
 *
 * Encode/decode IPP frames, CRC-16/CCITT, ring buffer receiver.
 * Compiled into both Main and Display firmware (and Bottlenose,
 * ported to ESP-IDF types).
 */

#ifndef IPP_H
#define IPP_H

#include "ipp_defs.h"
#include <string.h>

/* ──────────────────────────────────────────────────────────
 * CRC-16/CCITT (polynomial 0x1021, init 0xFFFF)
 * ────────────────────────────────────────────────────────── */
uint16_t ipp_crc16(const uint8_t *data, size_t len);
uint16_t ipp_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/* ──────────────────────────────────────────────────────────
 * Frame Encoding
 *
 * Serializes an ipp_frame_t into a byte buffer ready for UART TX.
 * Computes CRC over TYPE+LENGTH+PAYLOAD.
 * Returns total frame size, or 0 on error.
 * ────────────────────────────────────────────────────────── */
size_t ipp_encode(const ipp_frame_t *frame, uint8_t *out_buf, size_t buf_size);

/* ──────────────────────────────────────────────────────────
 * Convenience: build and encode a frame in one call.
 * Returns total frame size written to out_buf, or 0 on error.
 * ────────────────────────────────────────────────────────── */
size_t ipp_build(uint8_t seq, uint8_t type,
                 const void *payload, uint16_t payload_len,
                 uint8_t *out_buf, size_t buf_size);

/* ──────────────────────────────────────────────────────────
 * Ring Buffer Receiver
 *
 * Feed raw UART bytes into ipp_rx_feed(). When a complete,
 * CRC-valid frame is assembled, the callback fires.
 *
 * This is designed to run from an ISR — all functions are
 * safe for __not_in_flash_func() decoration.
 * ────────────────────────────────────────────────────────── */

// Callback type: called when a valid frame is received
typedef void (*ipp_rx_callback_t)(const ipp_frame_t *frame, void *user_data);

// Receiver state machine states
typedef enum {
    IPP_RX_WAIT_SYNC,
    IPP_RX_READ_SEQ,
    IPP_RX_READ_TYPE,
    IPP_RX_READ_LEN_LO,
    IPP_RX_READ_LEN_HI,
    IPP_RX_READ_PAYLOAD,
    IPP_RX_READ_CRC_LO,
    IPP_RX_READ_CRC_HI,
} ipp_rx_state_t;

// Receiver context (one per UART link)
typedef struct {
    ipp_rx_state_t  state;
    ipp_frame_t     frame;          // Frame being assembled
    uint16_t        payload_idx;    // Current position in payload
    uint8_t         crc_lo;         // Temp storage for CRC low byte

    ipp_rx_callback_t callback;     // Called on valid frame
    void             *user_data;    // Passed to callback

    // Stats
    uint32_t        frames_ok;
    uint32_t        frames_crc_err;
    uint32_t        frames_overflow;
} ipp_rx_ctx_t;

/**
 * Initialize a receiver context.
 */
void ipp_rx_init(ipp_rx_ctx_t *ctx, ipp_rx_callback_t cb, void *user_data);

/**
 * Feed a single byte into the receiver state machine.
 * Call this from the UART ISR for every received byte.
 * When a complete valid frame is assembled, ctx->callback fires.
 *
 * MUST be decorated with __not_in_flash_func() on RP2040.
 */
void ipp_rx_feed(ipp_rx_ctx_t *ctx, uint8_t byte);

/**
 * Feed a buffer of bytes (batch version).
 */
void ipp_rx_feed_buf(ipp_rx_ctx_t *ctx, const uint8_t *buf, size_t len);

/**
 * Reset receiver to initial state (e.g., after sync loss).
 */
void ipp_rx_reset(ipp_rx_ctx_t *ctx);

#endif // IPP_H
