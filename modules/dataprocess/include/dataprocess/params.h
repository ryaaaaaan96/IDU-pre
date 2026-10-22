#ifndef DP_PARAMS_H
#define DP_PARAMS_H

#include <dataprocess/points.h>

#define DP_PARAM_WRITE   BIT(0)
#define DP_PARAM_PERSIST BIT(1)
#define DP_PARAM_RESTORE BIT(2)
#define DP_PARAM_FORWARD BIT(3)
#define DP_PARAM_DEFER_STORE BIT(4)
#define DP_PARAM_STORAGE_READY BIT(0)
#define DP_PARAM_PENDING BIT(1)
#define DP_PARAM_HISTORY_RECOVERED BIT(2)
#define DP_PARAM_VALUE_CONVERTED BIT(3)
#define DP_PARAM_STORAGE_RESET BIT(4)

struct dp_param_status {
    uint32_t sequence;
    int error; /* Most recent operation result, independent of storage health. */
    uint16_t flags;
    /* Storage unavailable cause; cleared only after storage is ready again. */
    int storage_error;
};

/* Application-supplied access to downstream commands and absolute settings.
 * Volatile commands use write only; durable settings require readback.
 * Callbacks run synchronously under the parameter transaction lock; they may
 * block on their transport, but must not reenter parameter APIs. Return zero
 * on success or a transport error. The read callback must not replay writes.
 */
struct dp_params_transport {
    int (*read)(void *user, uint16_t dev_id, uint16_t address,
                uint16_t *words, size_t count);
    int (*write)(void *user, uint16_t dev_id, uint16_t address,
                 const uint16_t *words, size_t count);
    bool (*simulated)(void *user, uint16_t dev_id);
    void *user;
};

/* Copies callbacks before init; NULL clears them. Identical registrations are
 * idempotent. Replacing a backend after initialization returns -EBUSY.
 * A missing backend leaves local parameters usable; forwarded operations fail
 * with -ENODEV before writing a transaction intent or changing cached values.
 */
int dp_params_set_transport(const struct dp_params_transport *transport);

/* Mount only the supplied partition. Persist values by (dev_id, sig_id); defaults
 * and bounds remain in the process table. Incompatible record formats clear this
 * partition once, log a warning and set STORAGE_RESET; no old snapshot migration.
 * I/O errors disable persistent writes instead of erasing. Batch journals recover
 * complete value sets; downstream intents are reconciled by readback only.
 * Failed initialization leaves readable defaults. Values are desired configuration;
 * downstream live telemetry remains separate.
 */
int dp_params_init(const struct dp_equip_cfg *cfg, uint8_t area_id);
/* Collection feedback changes the live cache only; pending writes take priority. */
int dp_params_sample(const struct dp_point_update *values, size_t count,
                     uint16_t flags, int64_t sampled_at);
int dp_params_validate(const struct dp_equip_cfg *cfg);
int dp_params_read(struct dp_key key, struct dp_point_state *state);
/* Business-side writes use keys, the same validation/durability as Modbus.
 * error must be zero. Multi-point remote requests must be one contiguous unit.
 */
int dp_params_set_values(const struct dp_point_update *values, size_t count);
int dp_params_apply(const struct dp_point_update *values, size_t count, uint8_t flags);
/* Business worker flushes deferred values after the debounce deadline. */
int dp_params_flush_due(void);
int dp_params_sync_device(uint16_t dev_id);
/* Legacy address writer: caller must explicitly select an immutable mapping
 * view. Product protocols use TR; business code should prefer key-based writes. */
int dp_params_write_registers(const struct dp_equip_cfg *mapping, uint16_t address,
                               const uint16_t *words, size_t count);
int dp_params_restore(uint16_t dev_id);
int dp_params_flush(void);
/* Atomically flush and reject new writes until reboot (or cancellation). */
int dp_params_prepare_reboot(void);
void dp_params_cancel_reboot(void);
/* Reconcile a durable uncertain transaction by READBACK only; never replay it. */
int dp_params_recover(void);
/* Remount/reload durable configuration. Caller must quiesce application users. */
int dp_params_reload(void);
void dp_params_get_status(struct dp_param_status *status);
ZBUS_CHAN_DECLARE(dp_param_events);

#endif
