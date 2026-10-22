#ifndef TR_H
#define TR_H
#include <transmit/modbus_adapter.h>

/* Port controls require CONFIG_TR_MODBUS_ADAPTER. */
enum tr_port_mode { TR_MODE_SLAVE, TR_MODE_SLEEP, TR_MODE_CALLBACK };
/* Per-interface mode/filter handlers receive an accepted complete ADU. Return
 * -ENOENT to continue, 0 for a reply, TR_MODBUS_SILENT to consume silently,
 * or -errno/a positive Modbus exception. CALLBACK never falls through to a
 * normal register writer. Registrations are copied; user data must outlive use.
 */
int tr_port_set_handler(int iface, enum tr_port_mode mode,
                       tr_modbus_route_fn handler, void *user);
int tr_port_get_mode(int iface, enum tr_port_mode *mode);
/* Applies to following requests; a handler/TX already in progress may finish.
 * SLEEP suppresses new callbacks/replies without interrupting serial TX.
 */
int tr_port_set_mode(int iface, enum tr_port_mode mode);
int tr_port_set_filter(int iface, tr_modbus_route_fn handler, void *user);
int tr_port_enable_filter(int iface, bool enabled);
struct tr_port_stats {
    uint32_t requests;       /* Accepted requests reaching dispatch. */
    uint32_t responses;      /* Responses prepared, not physical TX completions. */
    uint32_t filtered;       /* Requests consumed by the configured filter. */
    uint32_t sleep_drops;    /* Valid frames discarded while sleeping. */
    uint32_t callback_errors;
};
enum tr_port_control { TR_PORT_GET_STATS, TR_PORT_CLEAR_STATS };
/* GET takes struct tr_port_stats *, CLEAR takes NULL. Requires the adapter. */
int tr_port_control(int iface, enum tr_port_control command, void *arg);
struct tr_connection_status {
    bool enabled, connected;
    int64_t last_activity;
};
int tr_port_connection_get(int iface, struct tr_connection_status *status);
struct tr_segment {
    uint16_t begin, end;
    uint8_t function; /* zero: any function with a register-address PDU */
    tr_modbus_route_fn handler;
    void *user;
    uint16_t target_dev, target_begin; /* used when handler is NULL */
};
struct tr_routes {
    const struct tr_segment *segments;
    size_t count;
};
/* Requires CONFIG_TR_MODBUS_ADAPTER; otherwise returns -ENOTSUP.
 * Direct read/dispatch APIs work without CONFIG_DP_PROCESS or CONFIG_COLLECT.
 */
int tr_attach(int iface, const struct tr_routes *routes);
int tr_dispatch(int iface, const struct modbus_adu *request,
                          struct modbus_adu *response, void *user);
struct dp_equip_cfg;
/* Startup-only immutable view. After any explicit binding, an unbound port
 * never falls back to the legacy global map. Failed validation preserves bindings. */
int tr_bind_mapping(uint16_t port, const struct dp_equip_cfg *view);
int tr_read(uint16_t port, uint8_t fc, uint16_t address, uint16_t *words, size_t count);
int tr_write(uint16_t port, uint16_t address, const uint16_t *words, size_t count);
int tr_read_register(uint8_t function, uint16_t address, uint16_t *value);
struct modbus_user_callbacks *tr_callbacks(void);
#endif
