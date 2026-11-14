/** @file @brief Zephyr RAW 从机的同步适配；不依赖 DP 或产品转发表。 */
#ifndef MODBUS_SUPPORT_SERVER_H
#define MODBUS_SUPPORT_SERVER_H
#include <zephyr/modbus/modbus.h>

/** @brief Modbus 数据区，区分线圈、离散输入、保持寄存器和输入寄存器。 */
typedef enum {
    MB_DATA_AREA_COIL,
    MB_DATA_AREA_DISCRETE_INPUT,
    MB_DATA_AREA_HOLDING_REGISTER,
    MB_DATA_AREA_INPUT_REGISTER,
} mb_data_area_t;

/** @brief Zephyr 已校验的整笔访问；寄存器为大端字节，位数据低位在前。 */
typedef struct {
    mb_data_area_t area;
    uint16_t first;
    uint16_t count;
    bool writing;
    uint8_t *data; /**< 读操作在预清零缓冲填充输出，写操作只读；仅在同步回调期间有效。 */
} mb_request_t;

/** @brief 每请求最多执行一次；返回 0 或 Modbus 异常码，负值归一为设备故障。 */
typedef int (*mb_access_t)(void *context, const mb_request_t *request);
typedef struct mb_server mb_server_t;

/** @brief 启动期占用一个官方 RAW 接口；数量由 MODBUS_NUMOF_RAW_ADU 限制。 */
int mb_server_open(uint8_t unit, mb_server_t **server);
/** @brief 用于启动失败回退或停止后的销毁；调用方先等待全部请求结束，执行中返回 -EBUSY。 */
int mb_server_close(mb_server_t *server);
/**
 * @brief 调用官方 modbus_raw_submit_rx，同步返回响应；不创建线程或等待系统工作队列。
 * @note 每实例调用必须串行，不支持递归。access 在调用线程执行。
 */
int mb_server_process(mb_server_t *server,
    const struct modbus_adu *request,
    struct modbus_adu *response,
    bool *respond,
    mb_access_t access,
    void *context);
#endif
