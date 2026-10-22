#include <transmit/registers.h>
#include "../../src/routes.h"
#include <zephyr/sys/byteorder.h>

static const DEV_TBL_CFG_S *protocol_table(const struct tr_register_service *service, DEV_TBL_CFG_S *view)
{
    if (!service) { return NULL; }
    if (service->segments) {
        *view = (DEV_TBL_CFG_S){.proto_type = PRO_ADDR_SLAVE,
            .pst_table = service->segments, .sig_num = service->segment_count};
        return view;
    }
    if (!service->device) { return NULL; }
    const DEV_TBL_CFG_S *tables = service->device->ptl_tbl_cfg;
    for (size_t i = 0; i < MAX_PTL_TBL_NUM; ++i) {
        if (tables[i].proto_type == PRO_ADDR_SLAVE) { return &tables[i]; }
    }
    return NULL;
}

static int validate_protocol_table(const struct tr_register_service *service)
{
    DEV_TBL_CFG_S view;
    const DEV_TBL_CFG_S *table = protocol_table(service, &view);
    if (service && ((service->segments && service->device) ||
                    (!service->segments && service->segment_count) ||
                    service->segment_count > UINT16_MAX)) { return -EINVAL; }
    if (!table) { return 0; } /* 没有协议段的设备只提供普通点表。 */
    if (!table->pst_table || !table->sig_num) { return -EINVAL; }
    const MB_TABLE_SEG_T *segments = table->pst_table;
    for (size_t i = 0; i < table->sig_num; ++i) {
        const MB_TABLE_SEG_T *a = &segments[i];
        if (a->reg_addr > a->end_addr || !a->func) { return -EINVAL; }
        for (size_t j = 0; j < i; ++j) {
            const MB_TABLE_SEG_T *b = &segments[j];
            if (a->reg_addr <= b->end_addr && b->reg_addr <= a->end_addr) {
                return -EEXIST;
            }
        }
    }
    if (!service->device) { return 0; }
    const DEV_TBL_CFG_S *tables = service->device->ptl_tbl_cfg;
    for (size_t i = 0; i < MAX_PTL_TBL_NUM; ++i) {
        if (&tables[i] != table && tables[i].proto_type == PRO_ADDR_SLAVE) {
            return -EEXIST;
        }
    }
    return 0;
}

int tr_register_route(int iface, const struct modbus_adu *rx,
                      struct modbus_adu *tx, void *user)
{
    const struct tr_register_service *service = user;
    if (iface < 0 || !rx || !tx || rx->length < 2 ||
        rx->length > sizeof(rx->data)) { return -EINVAL; }
    if (!rx->unit_id && (!service || !service->route_broadcast)) {
        return TR_MODBUS_SILENT;
    }
    DEV_TBL_CFG_S view;
    const DEV_TBL_CFG_S *table = protocol_table(service, &view);
    if (table) {
        uint16_t address = sys_get_be16(rx->data);
        const MB_TABLE_SEG_T *segments = table->pst_table;
        for (size_t i = 0; i < table->sig_num; ++i) {
            const MB_TABLE_SEG_T *s = &segments[i];
            if (address < s->reg_addr || address > s->end_addr) { continue; }
            tx->unit_id = rx->unit_id; tx->fc = rx->fc;
            tx->trans_id = rx->trans_id; tx->proto_id = rx->proto_id;
            int err = s->func(iface, rx, tx);
            /* -ENOENT 在路由层表示未处理；命中协议段后不能回退到 writer。 */
            return err == -ENOENT ? -ENOTSUP : err;
        }
    }
    /* 普通点表/地址段广播无副作用；协议段自行决定广播行为。 */
    if (!rx->unit_id) { return TR_MODBUS_SILENT; }
    return tr_dispatch(iface, rx, tx, service ? (void *)service->routes : NULL);
}

int tr_register_attach(int iface, const void *config)
{
    const struct tr_register_service *service = config;
    /* 所有段表校验通过后才替换现有路由。 */
    int err = validate_protocol_table(service);
    if (err) { return err; }
    enum tr_port_mode mode;
    err = tr_port_get_mode(iface, &mode);
    if (err) { return err; }
    err = tr_routes_validate(service ? service->routes : NULL);
    if (err) { return err; }
    if (service && service->mapping) {
        err = tr_bind_mapping(iface, service->mapping);
        if (err) { return err; }
    }
    err = tr_attach(iface, service ? service->routes : NULL);
    if (err) { return err; }
    err = tr_modbus_set_router(iface, tr_register_route, (void *)service);
    if (err) { return err; }
    const struct tr_modbus_policy policy = {
        .route_broadcast = service && service->route_broadcast,
    };
    return tr_modbus_set_policy(iface, &policy);
}

int tr_register_write(uint16_t address, const uint16_t *words, size_t count)
{
    int iface = tr_modbus_request_iface();
    return iface < 0 ? -EACCES : tr_write(iface, address, words, count);
}
