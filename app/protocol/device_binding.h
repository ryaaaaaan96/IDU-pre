/** @brief 产品设备与处理、采集表的装配配置。 */
#ifndef PROTOCOL_DEVICE_BINDING_H
#define PROTOCOL_DEVICE_BINDING_H
#include <dataprocess/table_registry.h>
/** @brief 未配置采集表时使用的默认值。 */
#define PROTOCOL_NO_COLLECT_TABLE 0U
/**
 * @brief 精简的产品声明，表行留在 ROM。
 *
 * collect_table 为零表示没有采集目标， 此时忽略 collect_port/address。
 */
struct data_device_cfg {
    uint8_t dev_id;
    uint16_t process_table;
    uint16_t collect_table;
    uint8_t collect_port;
    uint8_t collect_addr;
};
int protocol_bind_devices(const struct data_device_cfg *devices, size_t count,
                         DEV_TABLE_S *output);

#endif
