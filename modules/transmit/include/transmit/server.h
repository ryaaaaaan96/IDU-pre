#ifndef TR_SERVER_H
#define TR_SERVER_H

#include <transmit/modbus_adapter.h>

struct tr_server_config {
    const char *iface_name;
    struct modbus_iface_param param;
    tr_modbus_write_fn write;
    int (*attach)(int iface, const void *protocol);
    const void *protocol;
    int (*configure)(struct modbus_iface_param *param);
    uint32_t recovery_timeout_ms;
};

/* 应用线程周期调用。每个配置独占一个接口，模块保存各自的活动/待生效状态。
 * 配置必须为常驻对象；首次成功启动后不得修改，运行中只更新 unit 参数。
 * configure 可从业务参数生成最终串口设置；恢复期间下次调用重试。
 * 不可从当前端口的接收/重配置回调调用本函数。
 */
int tr_server_service(const struct tr_server_config *config, uint8_t unit);

#endif
