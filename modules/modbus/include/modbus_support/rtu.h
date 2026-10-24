#ifndef MODBUS_SUPPORT_RTU_H
#define MODBUS_SUPPORT_RTU_H

#include <zephyr/modbus/modbus.h>
#include <stddef.h>
#include <stdint.h>

/* 完整 RTU 帧包含地址、功能码和两个 CRC 字节。缓冲由调用方持有，
 * 不能与 ADU 重叠。此处不分配、提交、加锁或发完成通知。
 * 失败时输出不变；可在分配内存或获取接收所有权前校验，解码时也会校验输入。 */
int mb_rtu_validate(const uint8_t *frame, size_t size);
int mb_rtu_decode(const uint8_t *frame, size_t size, struct modbus_adu *adu);
int mb_rtu_encode(const struct modbus_adu *adu, uint8_t *frame,
                  size_t capacity, size_t *size);

#endif
