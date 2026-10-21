#ifndef CO_H
#define CO_H
#include <dataprocess/points.h>
#include <zephyr/modbus/modbus.h>
#define CO_MAX_REGS DP_TRANSFER_WORDS
enum co_port_mode { CO_PORT_POLL, CO_PORT_INIT, CO_PORT_UPDATE,
                     CO_PORT_IV, CO_PORT_FAULT, CO_PORT_PASSTHROUGH, CO_PORT_SLEEP };
enum co_device_online { CO_DEVICE_UNKNOWN, CO_DEVICE_OFFLINE,
                         CO_DEVICE_ONLINE, CO_DEVICE_EXCEPTION };
struct co_device_status {
    uint16_t dev_id;
    uint8_t bus_id;
    bool simulated;
    enum co_device_online state;
    int error;
    uint32_t successes, failures;
    int64_t last_success;
};
struct co_bus_cfg {
    uint8_t id;
    int iface;
    bool simulated;
    /* Optional protocol backend. In-place request/response; called under bus lock.
     * Without CO_MODBUS_ADAPTER, provide transfer and/or read_model.
     */
    int (*transfer)(void *user, struct modbus_adu *adu);
    int (*initialize)(void *user, uint16_t dev_id);
    int (*read_model)(void *user, uint16_t dev_id, uint16_t model,
                       uint16_t offset, uint16_t *words, size_t count);
    void *user;
};
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
int co_start(void);
/* All device bus IDs must fit CONFIG_CO_MAX_BUSES, including buses registered
 * later. Invalid configurations are rejected before installing callbacks.
 */
int co_init_bus(const struct dp_equip_cfg *cfg, const struct co_bus_cfg *bus);
int co_set_mode(uint8_t bus, enum co_port_mode mode);
int co_get_mode(uint8_t bus, enum co_port_mode *mode);
int co_enable_poll(size_t block, bool enabled);
int co_device_status(uint16_t dev_id, struct co_device_status *status);
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
