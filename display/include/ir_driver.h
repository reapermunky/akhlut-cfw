/**
 * ir_driver.h — NEC IR Receive/Transmit Driver
 *
 * Akhlut CFW
 *
 * GPIO interrupt-driven NEC decoder on IR_RX.
 * 38 kHz PWM carrier + timed bursts on IR_TX.
 */

#ifndef IR_DRIVER_H
#define IR_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ipp_defs.h"

void ir_driver_init(void);
bool ir_driver_poll(ipp_ir_code_t *out);
void ir_driver_send_nec(uint32_t code, uint8_t bits);

#endif
