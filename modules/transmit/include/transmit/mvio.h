#ifndef TR_MVIO_H
#define TR_MVIO_H

#include <transmit/registers.h>

struct tr_register_range { uint16_t first, last; };
struct tr_legacy_span { uint16_t first, points; uint8_t width; };

enum tr_mvio_register {
    TR_MVIO_REG_ADDRESS = 30459,
    TR_MVIO_REG_IAP_CAPABILITY = 20003,
    TR_MVIO_REG_IAP_VERSION = 31996,
};

struct tr_mvio_config {
    const struct tr_register_range *read_starts, *write_starts, *extensions;
    size_t read_start_count, write_start_count, extension_count;
    const struct tr_legacy_span *input, *holding;
    size_t input_count, holding_count;
    const struct tr_register_service *registers;
    /* 协议扩展可选；未配置时不依赖升级模块。 */
    int (*upgrade_route)(int iface, const struct modbus_adu *, struct modbus_adu *);
    bool (*upgrade_ready)(void);
};

/* 旧 MV-IO 的广播/247、跳空表遍历及 ACK-only 写规则，不属于标准 Modbus。
 * 配置须常驻，注册在接口启用之前进行。
 */
int tr_mvio_attach(int iface, const void *config);

#endif
