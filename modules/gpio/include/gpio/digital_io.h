#ifndef DIGITAL_IO_H
#define DIGITAL_IO_H

#include <zephyr/drivers/gpio.h>

enum digital_io_order { DIGITAL_IO_LSB_FIRST, DIGITAL_IO_MSB_FIRST };
struct digital_io_config {
    const struct gpio_dt_spec *pins;
    size_t count;
    enum digital_io_order order;
};

/* 支持 1..32 路输入，逻辑有效电平取自 gpio_dt_spec；
 * 不管理引脚所有权，也不解释设备专用地址。读取失败时保持 *bits 不变。
 * GPIO 顺序读取，不构成电气上同时采样的快照。
 */
int digital_io_init(const struct digital_io_config *config);
int digital_io_read(const struct digital_io_config *config, uint32_t *bits);

#endif
