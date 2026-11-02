/** @file @brief 从机协议统一入口与实例登记；APP 管理执行线程。 */
#ifndef TRANSMIT_H
#define TRANSMIT_H

#include <stddef.h>
#include <stdint.h>

#include <transmit/mapping.h>
#include <transmit/modbus.h>
#include <transmit/protocol.h>
#include <vport/vport.h>

typedef struct {
    vport_t *port;
    const tr_protocol_t *protocols;
    size_t protocol_count;
    size_t rx_capacity;
    size_t tx_capacity;
    uint32_t send_timeout_ms;
} tr_config_t;

/** @brief 登记单实例，内部申请固定缓存并绑定端口；配置常驻只读，失败不绑定。 */
int tr_register(const tr_config_t *config);

#endif
