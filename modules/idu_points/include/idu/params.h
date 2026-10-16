#ifndef IDU_PARAMS_H
#define IDU_PARAMS_H

#include <idu/points.h>

#define IDU_PARAM_WRITE   BIT(0)
#define IDU_PARAM_PERSIST BIT(1)
#define IDU_PARAM_RESTORE BIT(2)
#define IDU_PARAM_FORWARD BIT(3)
#define IDU_PARAM_DEFER_STORE BIT(4)
#define IDU_PARAM_STORAGE_READY BIT(0)
#define IDU_PARAM_PENDING BIT(1)
#define IDU_PARAM_HISTORY_RECOVERED BIT(2)
#define IDU_PARAM_SCHEMA_MIGRATED BIT(3)

struct idu_param_status {
    uint32_t sequence;
    int error;
    uint16_t flags;
};

/* Initialization mounts only the supplied partition. No erase/format fallback.
 * Failed initialization leaves readable defaults and rejects writes. Values are
 * desired configuration; downstream live telemetry remains separate.
 */
int idu_params_init(const struct idu_equip_cfg *cfg, uint8_t area_id,
                    int client_iface, bool simulated);
int idu_params_validate(const struct idu_equip_cfg *cfg);
int idu_params_read(struct idu_key key, struct idu_point_state *state);
/* Business-side writes use keys, the same validation/durability as Modbus.
 * error must be zero. Multi-point remote requests must be one contiguous unit.
 */
int idu_params_set_values(const struct idu_point_update *values, size_t count);
int idu_params_apply(const struct idu_point_update *values, size_t count, uint8_t flags);
/* Business worker flushes deferred values after the debounce deadline. */
int idu_params_flush_due(void);
int idu_params_sync_device(uint16_t dev_id);
int idu_params_write_registers(uint16_t address, const uint16_t *words, size_t count);
int idu_params_restore(uint16_t dev_id);
int idu_params_flush(void);
/* Atomically flush and reject new writes until reboot (or cancellation). */
int idu_params_prepare_reboot(void);
void idu_params_cancel_reboot(void);
/* Reconcile a durable uncertain transaction by READBACK only; never replay it. */
int idu_params_recover(void);
/* Remount/reload durable configuration. Caller must quiesce application users. */
int idu_params_reload(void);
void idu_params_get_status(struct idu_param_status *status);
ZBUS_CHAN_DECLARE(idu_param_events);

#endif
