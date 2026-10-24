/**
 * @file
 * @brief 完整 Modbus RTU 报文的无分配编解码接口。
 *
 * 完整 RTU 帧包含地址、功能码和两个 CRC 字节。
 *
 * 缓冲由调用方持有， 不能与 ADU 重叠。此处不分配、提交、加锁或发完成通知。 失败时输出不变；可在分配内存或获取接收所有权前校验，解码时也会校验输入。
 */
#ifndef MODBUS_SUPPORT_RTU_H
#define MODBUS_SUPPORT_RTU_H

#include <zephyr/modbus/modbus.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief 校验完整 RTU 帧的长度和 CRC。
 * @param[in] frame 待校验的完整报文字节。
 * @param[in] size 报文字节数。
 * @return 有效返回 0，否则返回负错误码。
 */
int mb_rtu_validate(const uint8_t *frame, size_t size);
/**
 * @brief 校验完整 RTU 帧并解码为 ADU。
 * @param[in] frame 完整报文字节，不得与 adu 重叠。
 * @param[in] size 报文字节数。
 * @param[out] adu 接收解码结果；失败时保持原值。
 * @return 成功返回 0，失败返回负错误码。
 */
int mb_rtu_decode(const uint8_t *frame, size_t size, struct modbus_adu *adu);
/**
 * @brief 将 ADU 编码为包含 CRC 的完整 RTU 帧。
 * @param[in] adu 待编码的 ADU，不得与 frame 重叠。
 * @param[out] frame 接收编码报文的缓冲区。
 * @param[in] capacity 缓冲区字节容量。
 * @param[out] size 接收实际报文字节数；失败时保持原值。
 * @return 成功返回 0，失败返回负错误码。
 */
int mb_rtu_encode(const struct modbus_adu *adu, uint8_t *frame,
                  size_t capacity, size_t *size);

#endif
