#ifndef CO_MODEL_SIMULATOR_H
#define CO_MODEL_SIMULATOR_H
#include <platform_callback.h>

struct co_model_simulator {
    uint16_t first_device, last_device;
    const UPP_M_MOD_REG *registry;
    PARA_GET_RANGE_DEF defaults;
};

/* co_bus_cfg.read_model 回调，user 指向常驻 co_model_simulator 配置。
 * 只返回模型默认值，不实现 UPP 线上协议。
 */
int co_model_simulator_read(void *user, uint16_t dev, uint16_t model,
                            uint16_t offset, uint16_t *words, size_t count);
#endif
