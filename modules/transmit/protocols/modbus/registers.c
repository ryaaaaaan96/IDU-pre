#include <transmit/registers.h>

int tr_register_route(int iface, const struct modbus_adu *rx,
                      struct modbus_adu *tx, void *user)
{
    const struct tr_register_service *service = user;
    if (!rx || !tx) { return -EINVAL; }
    if (service && service->extension) {
        int err = service->extension(iface, rx, tx);
        if (err != -ENOENT) { return err; }
    }
    /* 普通点表/地址段广播无副作用；扩展协议自行决定广播行为。 */
    if (!rx->unit_id) { return TR_MODBUS_SILENT; }
    return tr_dispatch(iface, rx, tx, service ? (void *)service->routes : NULL);
}

int tr_register_attach(int iface, const void *config)
{
    const struct tr_register_service *service = config;
    /* 先由通用地址段接口完成配置校验，失败不替换现有路由。 */
    int err = tr_attach(iface, service ? service->routes : NULL);
    if (err) { return err; }
    err = tr_modbus_set_router(iface, tr_register_route, (void *)service);
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
