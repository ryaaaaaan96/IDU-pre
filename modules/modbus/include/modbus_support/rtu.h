#ifndef MODBUS_SUPPORT_RTU_H
#define MODBUS_SUPPORT_RTU_H

#include <zephyr/modbus/modbus.h>
#include <stddef.h>
#include <stdint.h>

/* Complete RTU frames include address, function and two CRC bytes.
 * Buffers belong to the caller and must not overlap the ADU. No allocation,
 * submission, locking or completion notification is performed here.
 * Outputs remain unchanged on failure. Validation can precede allocation or
 * acquisition of receive ownership; decoding also validates its input. */
int mb_rtu_validate(const uint8_t *frame, size_t size);
int mb_rtu_decode(const uint8_t *frame, size_t size, struct modbus_adu *adu);
int mb_rtu_encode(const struct modbus_adu *adu, uint8_t *frame,
                  size_t capacity, size_t *size);

#endif
