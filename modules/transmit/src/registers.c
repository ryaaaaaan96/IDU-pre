#include <transmit/registers.h>
#include <dataprocess/params.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

static int read_status(const struct tr_register_service *service,
                        const struct dp_param_status *status,
                        uint16_t address, uint16_t *value)
{
    for (size_t i = 0; service && i < service->status_count; ++i) {
        if (service->status[i].address != address) { continue; }
        switch (service->status[i].field) {
        case TR_PARAMETER_FLAGS: *value = status->flags; break;
        case TR_PARAMETER_ERROR: *value = (uint16_t)(int16_t)status->error; break;
        case TR_PARAMETER_SEQUENCE_LOW: *value = status->sequence & 0xffff; break;
        case TR_PARAMETER_SEQUENCE_HIGH: *value = status->sequence >> 16; break;
        default: return -EINVAL;
        }
        return 0;
    }
    return -ENOENT;
}

int tr_register_route(int iface, const struct modbus_adu *rx,
                      struct modbus_adu *tx, void *user)
{
    const struct tr_register_service *service = user;
    if (service && ((service->command_count && !service->commands) ||
                    (service->status_count && !service->status))) { return -EINVAL; }
    if (!rx || !tx) { return -EINVAL; }
    if (service && service->extension) {
        int err = service->extension(iface, rx, tx);
        if (err != -ENOENT) { return err; }
    }
    /* 普通参数广播无副作用；扩展协议自行决定广播行为。 */
    if (!rx->unit_id) { return TR_MODBUS_SILENT; }
    int err = tr_dispatch(iface, rx, tx, NULL);
    if (err != -ENOENT) { return err; }
    /* tr_dispatch 已校验完整请求的长度、数量和地址边界。 */
    uint16_t address = sys_get_be16(rx->data);
    uint16_t count = rx->fc == 6 ? 1 : sys_get_be16(rx->data + 2);
    if (rx->fc == 3 || rx->fc == 4) {
        struct dp_param_status status = {0};
        if (rx->fc == 3 && service && service->status_count) {
            /* 同一响应中的标志、错误和序号来自同一次参数状态快照。 */
            dp_params_get_status(&status);
        }
        for (size_t i = 0; i < count; ++i) {
            uint16_t value;
            err = rx->fc == 3 ? read_status(service, &status, address + i, &value) : -ENOENT;
            if (err == -ENOENT) { err = tr_read_register(rx->fc, address + i, &value); }
            if (err) { return err; }
            sys_put_be16(value, tx->data + 1 + 2 * i);
        }
        tx->data[0] = count * 2;
        tx->length = 1 + count * 2;
        return 0;
    }
    if (rx->fc != 6 && rx->fc != 16) { return -ENOENT; }
    for (size_t i = 0; service && i < service->command_count; ++i) {
        const struct tr_command *command = &service->commands[i];
        if (address != command->address) { continue; }
        if (count != 1 || sys_get_be16(rx->data + (rx->fc == 6 ? 2 : 5)) !=
            command->expected_value || !command->execute) { return -EINVAL; }
        err = command->execute(command->dev_id);
        /* 命令已命中，业务错误不能被路由层解释成“继续查找”或“静默”。 */
        if (err == -ENOENT) { return MODBUS_EXC_ILLEGAL_DATA_ADDR; }
        if (err == TR_MODBUS_SILENT) { return MODBUS_EXC_SERVER_DEVICE_FAILURE; }
        if (err) { return err; }
        tx->length = 4;
        memcpy(tx->data, rx->data, 4);
        return 0;
    }
    return -ENOENT;
}

int tr_register_attach(int iface, const void *config)
{
    const struct tr_register_service *service = config;
    if (service && ((service->command_count && !service->commands) ||
                    (service->status_count && !service->status))) { return -EINVAL; }
    int err = tr_modbus_set_router(iface, tr_register_route, (void *)service);
    if (err) { return err; }
    const struct tr_modbus_policy policy = {
        .route_broadcast = service && service->extension,
    };
    return tr_modbus_set_policy(iface, &policy);
}

int tr_register_write(uint16_t address, const uint16_t *words, size_t count)
{
    int iface = tr_modbus_request_iface();
    return iface < 0 ? -EACCES : tr_write(iface, address, words, count);
}
