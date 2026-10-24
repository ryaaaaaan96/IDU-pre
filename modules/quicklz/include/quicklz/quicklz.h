/**
 * @file
 * @brief 有界 QuickLZ 数据块解码接口。
 */
#ifndef QUICKLZ_H
#define QUICKLZ_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief 解码完整的 QuickLZ 1.5 级别 3 数据块，不保留流式历史。
 *
 * 支持压缩块和原样存储块。缓冲不可重叠，cap 必须能用 int 表示； 不分配内存或保留状态。返回解码字节数；
 * 输入格式错误或目标空间不足返回 -EBADMSG。失败时目标内容未定义。
 */
int qlz_decode_level3(const uint8_t *src, size_t len, uint8_t *dst, size_t cap);

#endif
