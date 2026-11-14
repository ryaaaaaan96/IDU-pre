/** @file @brief RTU 流检查；边界由实际接收时序产生，检查不执行业务。 */
#ifndef MODBUS_RTU_FRAME_H
#define MODBUS_RTU_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/** @brief 成功输出完整帧长；等待返回 -EAGAIN，不匹配返回 -EBADMSG。 */
int mb_rtu_probe(const uint8_t *data,
    size_t size,
    size_t boundary_size,
    bool at_boundary,
    bool broken,
    bool expired,
    size_t *frame_size);
#endif
