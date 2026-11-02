/** @file @brief 协议匹配、执行回调与协议配置。 */
#ifndef TRANSMIT_PROTOCOL_H
#define TRANSMIT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <transmit/modbus.h>

typedef enum {
    TR_WAIT,
    TR_MISMATCH,
    TR_COMPLETE,
} tr_match_t;

/** @brief 未消费缓存视图；回调不能持有指针，不允许自行删除字节或执行副作用。 */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t boundary_size;
    bool at_boundary;
    bool broken;
    bool expired;
} tr_view_t;

/** @brief 同一候选视图可重复检查；不能依赖隐含字节游标或保留跨候选解析状态。 */
typedef tr_match_t (*tr_check_t)(void *context, const tr_view_t *view, size_t *consumed);
/** @brief 已选择的完整报文；输出长度零表示静默，返回负值不重放请求。 */
typedef int (*tr_execute_t)(void *context,
    const uint8_t *request,
    size_t size,
    uint8_t *response,
    size_t capacity,
    size_t *response_size);

typedef enum {
    TR_PROTOCOL_MODBUS,
    TR_PROTOCOL_CUSTOM,
} tr_protocol_type_t;

/** @brief 每实例每类最多一个；自定义入口用于应用现有协议，不建立动态协议注册中心。 */
typedef struct {
    tr_protocol_type_t type;
    size_t max_frame;
    uint32_t timeout_us;

    union {
        tr_modbus_t modbus;

        struct {
            tr_check_t check;
            tr_execute_t execute;
            void *context;
        } custom;
    };
} tr_protocol_t;

#endif
