/**
 * @file
 * @brief 数字量输入配置和逻辑值读取接口。
 *
 * 支持 1..32 路输入，逻辑有效电平取自 gpio_dt_spec； 不管理引脚所有权，也不解释设备专用地址。
 *
 * 读取失败时保持 *bits 不变。 GPIO 顺序读取，不构成电气上同时采样的快照。
 */
#ifndef DIGITAL_IO_H
#define DIGITAL_IO_H

#include <zephyr/drivers/gpio.h>

/**
 * @brief 输入引脚在结果位图中的排列顺序。
 */
typedef enum { DIGITAL_IO_LSB_FIRST, DIGITAL_IO_MSB_FIRST } digital_io_order_t;
/**
 * @brief 数字量引脚与位序配置。
 */
struct digital_io_config {
    const struct gpio_dt_spec *pins;
    size_t count;
    digital_io_order_t order;
};

/**
 * @brief 校验配置并将所有输入引脚配置为输入模式。
 * @param[in] config 输入引脚、数量及逻辑位序配置。
 * @return 成功返回 0，失败返回负错误码。
 * @note 初始化部分失败时，此前成功配置的引脚不回滚。
 */
int digital_io_init(const struct digital_io_config *config);
/**
 * @brief 按配置位序读取逻辑输入位图。
 * @param[in] config 输入配置，使用期间保持不变。
 * @param[out] bits 接收逻辑位图；失败时保持原值。
 * @return 成功返回 0，失败返回负错误码。
 */
int digital_io_read(const struct digital_io_config *config, uint32_t *bits);

#endif
