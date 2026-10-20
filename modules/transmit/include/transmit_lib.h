#ifndef TRANSMIT_LIB_H
#define TRANSMIT_LIB_H

#include <stdint.h>
#include <stdbool.h>
#include <transmit/transmit.h>
#include <dataprocess/reference_tables.h>

typedef enum {
    MB_MODE_SLAVE = 0, MB_MODE_SLEEP = 1, MB_MODE_CALLBACK = 2, MB_MODE_MAX,
} MB_MODE_E;
typedef enum {
    MB_PORT_SET_CHECK_REG_MAX = 0,
    MB_PORT_GET_RECV_OK_TIMES,
    MB_PORT_GET_RECV_ERR_TIMES,
    MB_PORT_INC_RECV_OK_TIMES,
    MB_PORT_INC_RECV_ERR_TIMES,
    MB_PORT_CLR_RECV_ERR_TIMES,
    MB_PORT_GET_SEND_OK_TIMES,
    MB_PORT_CLR_SEND_OK_TIMES,
    MB_PORT_GET_RECV_MB_TIMES,
    MB_PORT_CLR_RECV_MB_TIMES,
    MB_PORT_RESET_CFG_PARA,
    MB_PORT_CTRL_ENUM_MAX,
} MB_PORT_CTRL_E;

/* These port APIs require CONFIG_TR_MODBUS_ADAPTER.
 * Deliberate Zephyr interface: an accepted complete ADU, not ARC/raw bytes.
 * Callbacks must validate function-specific PDU lengths before accessing data.
 * port_id is a Zephyr Modbus interface index. No user pointer or ARC dependency.
 * Mode handler: -1 continues to normal routing, 0 replies using response.
 * Packet filter: 0 continues, 4 consumes silently. Other results are unsupported;
 * in particular AES=2 is not a Modbus exception. For native -errno/exception/
 * silence semantics and per-port handlers, use tr_port_* instead.
 */
typedef int (*TR_FUNC_CB)(uint16_t port_id, const struct modbus_adu *request,
                          struct modbus_adu *response);
/* Global per-mode fallback, including interfaces initialized later. Explicit
 * tr_port_set_handler registrations take precedence. SLEEP has no callback.
 * NULL unregisters, unless it would leave a port in CALLBACK without a handler.
 */
int tr_reg_table_func(MB_MODE_E work_mode, TR_FUNC_CB table_func);
int tr_reg_pkt_filter_func(uint16_t port_id, TR_FUNC_CB filter);
int tr_pkt_filter_config(uint16_t port_id, bool enabled);
int tr_get_port_mode(uint16_t port_id, uint8_t *mode);
int tr_set_port_mode(uint16_t port_id, uint8_t mode);
/* GET_RECV_MB writes uint32_t accepted complete ADUs reaching dispatch;
 * CLR_RECV_MB clears that count (NULL arg). All other reference commands return
 * -ENOTSUP without reading/writing arg: physical CRC/TX counters and raw port
 * configuration are not exposed by this adapter. Native detailed statistics
 * remain available through tr_port_control(). All APIs return 0 or -errno.
 */
int tr_mb_port_work_control(uint16_t port_id, uint16_t command, void *arg);

#endif
