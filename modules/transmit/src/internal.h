#ifndef TR_INTERNAL_H
#define TR_INTERNAL_H
#include <transmit/transmit.h>
/** @brief 校验通用映射配置，不读取 DP 或执行业务回调。 */
int tr_mapping_validate(const tr_mapping_t *mapping);
/** @brief 先按对外类型读取 DP，再放缩并以对外类型保存；输出为本地数值。 */
int tr_mapping_read(const tr_mapping_t *mapping, value_t *value);
/** @brief 仅预检写入换算，不访问 DP，不产生业务副作用。 */
int tr_mapping_check_write(const tr_mapping_t *mapping, const value_t *value);
/** @brief 对外数值按需逆放缩后，仍按对外类型表示并写入 DP。 */
int tr_mapping_write(const tr_mapping_t *mapping, const value_t *value);

int tr_modbus_validate(const tr_modbus_t *config);
int tr_modbus_execute(mb_server_t *server,
    const tr_modbus_t *config,
    const uint8_t *request,
    size_t size,
    uint8_t *response,
    size_t capacity,
    size_t *response_size);
#endif
