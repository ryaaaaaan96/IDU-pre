#ifndef CO_H
#define CO_H
#include <dataprocess/points.h>
#include <zephyr/modbus/modbus.h>
#define CO_MAX_REGS DP_TRANSFER_WORDS
enum co_port_mode { CO_PORT_POLL, CO_PORT_INIT, CO_PORT_UPDATE,
                     CO_PORT_IV, CO_PORT_FAULT, CO_PORT_PASSTHROUGH, CO_PORT_SLEEP };
enum co_device_online { CO_DEVICE_UNKNOWN, CO_DEVICE_OFFLINE,
                         CO_DEVICE_ONLINE, CO_DEVICE_EXCEPTION, CO_DEVICE_INITIALIZING };
struct co_device_status {
    uint16_t dev_id;
    uint8_t bus_id;
    bool simulated;
    enum co_device_online state;
    int error;
    uint32_t successes, failures;
    int64_t last_success;
    bool online; /* A valid response was seen; initialization may still fail. */
    int last_error; /* History, retained across subsequent successful operations. */
    int64_t last_response;
};
struct co_device_policy {
    uint8_t retries; /* Extra initialization/poll attempts; offline probes never retry. */
    uint32_t exception_ms;
    uint32_t probe_ms; /* Delay from the end of an offline probe to the next one. */
};
/* Startup-only immutable policy, one per managed device. No policy leaves
 * explicit/manual acquisition available without automatic device discovery.
 */
int co_set_device_policy(uint16_t dev_id, const struct co_device_policy *policy);
/* 使用应用传输回调时没有 Zephyr 接口；其他可选后端均默认关闭。 */
#define CO_BUS_DEFAULT_IFACE (-1)
#define CO_BUS_DEFAULT_SIMULATED false
#define CO_BUS_DEFAULT_INITIALIZE NULL
#define CO_BUS_DEFAULT_READ_MODEL NULL
#define CO_BUS_DEFAULT_USER NULL

struct co_bus_cfg {
    uint8_t id;
    int iface;
    bool simulated;
    /* Optional protocol backend. In-place request/response; called under bus lock.
     * Without CO_MODBUS_ADAPTER, provide transfer and/or read_model.
     */
    /* Honor the remaining total timeout. Return EBUSY for pre-send contention;
     * ETIMEDOUT denotes a transmitted request with no response. */
    int (*transfer)(void *user, uint16_t dev_id, struct modbus_adu *adu,
                    k_timeout_t timeout);
    int (*initialize)(void *user, uint16_t dev_id);
    int (*read_model)(void *user, uint16_t dev_id, uint16_t model,
                       uint16_t offset, uint16_t *words, size_t count);
    void *user;
};
/* Application wires the data consumer explicitly. Callbacks are copied before
 * starting workers; sample must copy/accept the batch before returning.
 * sync requests completion and quality validation for initialization samples.
 */
struct co_data_callbacks {
    int (*sample)(struct dp_point_batch *batch, bool sync);
    /* Explicit co_initialize_device completion only. Automatic discovery reads
     * its configured packets without replaying business INIT or saved writes.
     */
    int (*initialize)(uint16_t dev_id);
    /* Actual offline transition only, outside CO locks. Retain the timestamp. */
    int (*offline)(uint16_t dev_id, int error, int64_t observed_at);
};
int co_set_data_callbacks(const struct co_data_callbacks *callbacks);
struct dp_params_transport;
/* Optional ready-to-register synchronous downstream parameter adapter. */
const struct dp_params_transport *co_parameter_transport(void);
struct co_request {
    uint32_t id;
    uint16_t dev_id;
    uint8_t priority;
    enum co_port_mode mode; /* Only scheduled while the port is in this mode. */
    struct modbus_adu adu;
};
typedef void (*co_done_fn)(const struct co_request *reply,
                                    int error, void *user);
int co_init(const struct dp_equip_cfg *cfg, int iface, bool simulated);
int co_poll(size_t block);
/* Validate/arm registered buses; does not create threads. Then run one
 * application-owned worker per bus, passing (void *)(uintptr_t)bus_id.
 */
int co_polling_init(void);
void co_bus_task(void *bus_id, void *unused1, void *unused2);
/* All device bus IDs must fit CONFIG_CO_MAX_BUSES, including buses registered
 * later. Invalid configurations are rejected before installing callbacks.
 */
int co_init_bus(const struct dp_equip_cfg *cfg, const struct co_bus_cfg *bus);
int co_set_mode(uint8_t bus, enum co_port_mode mode);
int co_get_mode(uint8_t bus, enum co_port_mode *mode);
int co_enable_poll(size_t block, bool enabled);
int co_device_status(uint16_t dev_id, struct co_device_status *status);
/* Clear RAM errors and request counters only; preserve scheduling and quality. */
int co_clear_device_errors(uint16_t dev_id);
int co_initialize_device(uint16_t dev_id);
/* Copies requests into the bounded per-bus queue and wakes its worker.
 * An accepted request completes once; only its matching port mode runs it.
 */
int co_submit(const struct co_request *request,
                        co_done_fn done, void *user);
int co_cancel(uint8_t bus); /* queued requests complete with -ECANCELED */
int co_transfer_mode(uint16_t dev_id, enum co_port_mode mode, struct modbus_adu *adu);
/* Ordinary reads/writes require POLL, or the owning initialization context. */
int co_transfer(uint16_t dev_id, struct modbus_adu *adu);
int co_read(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count);
int co_write(uint16_t dev_id, uint16_t address, const uint16_t *words, size_t count);
int co_reconfigure(uint8_t bus, struct modbus_iface_param param);
ZBUS_CHAN_DECLARE(co_device_events);
#endif
