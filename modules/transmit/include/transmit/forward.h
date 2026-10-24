#ifndef TR_FORWARD_H
#define TR_FORWARD_H
#include <transmit/registers.h>

#define TR_FORWARD_DEFAULT_SEGMENTS 0U
#define TR_FORWARD_DEFAULT_BROADCAST false

/* ROM 配置。forward_id 使用独立于 DP 键的命名空间；
 * port_id 表示已登记的 TR 完整报文端口，
 * 不是 UART 或站号。站号由应用在显式初始化时提供。 */
struct tr_forward_cfg {
    uint16_t forward_id;
    uint16_t port_id;
    uint16_t transmit_table;
    uint16_t protocol_segments;
    bool route_broadcast;
};
/* 仅在启动期安装到明确的 Modbus 接口。配置和登记表保持不变；
 * 失败声明不得替换活动条目。 */
int tr_forward_attach(int iface, const struct tr_forward_cfg *config);
int tr_forward_init(const struct tr_forward_cfg *config, uint8_t unit);
/* 周期维护，不补接或初始化缺失条目。 */
int tr_forward_service(const struct tr_forward_cfg *config, uint8_t unit);
#endif
