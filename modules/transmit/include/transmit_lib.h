#ifndef TRANSMIT_LIB_H
#define TRANSMIT_LIB_H

#include <stdint.h>
#include <stdbool.h>
#include <transmit/transmit.h>
#include <dataprocess/reference_tables.h>

/* 转发表默认字序；寄存器地址、设备、信号、功能码及长度均由应用填写。 */
#define TR_TABLE_DEFAULT_BYTE_ORDER BYTE_ORDER_DEFAULT

/* 参考地址段表布局；地址直接使用线地址，不隐式加一。
 * 回调使用 Zephyr 完整 ADU，负责功能码、长度及广播语义。
 * 命中后不再回退；-ENOENT 也作为非法地址返回给请求方。
 */
typedef int (*MB_SEG_CB)(int port_id, const struct modbus_adu *request,
                         struct modbus_adu *response);
typedef struct mb_table_seg_t {
    uint16_t reg_addr;
    uint16_t end_addr;
    MB_SEG_CB func;
} MB_TABLE_SEG_T, MB_TABLE_SEG_S;

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

/* 这些端口接口要求 CONFIG_TR_MODBUS_ADAPTER。
 * 此处采用 Zephyr 专用接口，输入为已准入的完整 ADU，
 * 不接受 ARC 或原始字节。回调访问数据前须校验对应功能码的 PDU 长度。
 * port_id 是 Zephyr Modbus 接口索引，
 * 不含用户指针或 ARC 依赖。模式处理函数返回 -1 继续普通路由，
 * 0 使用 response 回复。报文过滤器返回 0 继续，4 静默接收；
 * 其他结果不支持，尤其 AES=2 不是 Modbus 异常。
 * 需要原生 -errno、异常、静默及逐端口处理语义时，使用 tr_port_*。
 */
typedef int (*TR_FUNC_CB)(uint16_t port_id, const struct modbus_adu *request,
                          struct modbus_adu *response);
/* 各模式的全局后备处理器，也作用于稍后初始化的接口。
 * 显式 tr_port_set_handler 优先；SLEEP 没有回调。
 * NULL 取消登记，但不允许让 CALLBACK 端口失去处理函数。
 */
int tr_reg_table_func(MB_MODE_E work_mode, TR_FUNC_CB table_func);
int tr_reg_pkt_filter_func(uint16_t port_id, TR_FUNC_CB filter);
int tr_pkt_filter_config(uint16_t port_id, bool enabled);
int tr_get_port_mode(uint16_t port_id, uint8_t *mode);
int tr_set_port_mode(uint16_t port_id, uint8_t mode);
/* 启用按已准入 ADU 活动判断的连接监控，超时为 CONFIG_TR_CONN_TIMEOUT_MS。
 * 通过 tr_port_connection_get() 观察，不推断物理载波。
 */
int tr_port_conn_status_config(uint16_t port_id, bool enabled);
/* GET_RECV_MB 向 uint32_t 写入到达分发阶段的已准入完整 ADU 数；
 * CLR_RECV_MB 清除此计数，参数为 NULL。
 * 其余参考命令均返回 -ENOTSUP，且不读写 arg：
 * 此适配实现不提供物理 CRC/TX 计数或原始端口配置。
 * 原生详细统计仍可通过 tr_port_control() 获取；
 * 所有接口返回 0 或 -errno。
 */
int tr_mb_port_work_control(uint16_t port_id, uint16_t command, void *arg);

#endif
