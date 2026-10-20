#ifndef TR_MODBUS_ADAPTER_H
#define TR_MODBUS_ADAPTER_H

#include <zephyr/modbus/modbus.h>
#include <errno.h>

/* Called once for a complete FC06/FC16 PDU; return 0 or -errno. Never per-word.
 * isolated=true is required if callback waits for downstream Modbus or flash.
 * A non-isolated fixture callback must not block on protocol transactions.
 */
typedef int (*tr_modbus_write_fn)(uint16_t address, const uint16_t *words, size_t count);
/* Startup calls are serialized. An occupied client/server returns -EALREADY
 * without changing its writer or queue. Failed startup releases a newly
 * reserved worker slot. Do not race startup with direct modbus_init_* calls.
 */
int tr_modbus_init_server(int iface, struct modbus_iface_param param,
                           tr_modbus_write_fn write_fn, bool isolated);
/* Return -ENOENT to continue, TR_MODBUS_SILENT to consume without replying.
 * Broadcasts reach a router only with an explicit per-interface policy.
 */
#define TR_MODBUS_SILENT (-ECANCELED)
typedef int (*tr_modbus_route_fn)(int iface, const struct modbus_adu *request,
                                  struct modbus_adu *response, void *user);
int tr_modbus_set_router(int iface, tr_modbus_route_fn route, void *user);
struct tr_modbus_policy {
    /* NULL keeps the standard own-unit/broadcast filter. */
    bool (*accept)(const struct modbus_adu *request, uint8_t own_unit, void *user);
    bool route_broadcast; /* Router owns broadcast handling and reply policy. */
    void *user;
};
/* Startup only, copied. NULL restores standard unit/broadcast filtering. */
int tr_modbus_set_policy(int iface, const struct tr_modbus_policy *policy);
int tr_modbus_request_iface(void);
uint8_t tr_modbus_request_function(void);
typedef void (*tr_modbus_config_done_fn)(int iface, int error, void *user);
/* Copied request; applied after the old response has completed. */
int tr_modbus_reconfigure(int iface, struct modbus_iface_param param,
                           tr_modbus_config_done_fn done, void *user);
/* Reset the current backend even if TX never completed. Executing callbacks
 * must finish first; never cancel a handler in the middle of a flash write. */
int tr_modbus_recover(int iface, tr_modbus_config_done_fn done, void *user);
/* Main-loop health service: idle/blocked TX triggers recovery after timeout_ms;
 * a stuck handler returns -ETIMEDOUT so the board watchdog can reset the CPU. */
int tr_modbus_service(int iface, uint32_t timeout_ms);
struct tr_modbus_health { uint32_t recoveries; int last_error; bool changing; };
int tr_modbus_get_health(int iface, struct tr_modbus_health *health);

#endif
