#ifndef IDU_COLLECT_H
#define IDU_COLLECT_H
#include <idu/points.h>
enum idu_port_mode { IDU_PORT_POLL, IDU_PORT_INIT, IDU_PORT_UPDATE,
                     IDU_PORT_IV, IDU_PORT_FAULT, IDU_PORT_PASSTHROUGH, IDU_PORT_SLEEP };
enum idu_device_online { IDU_DEVICE_UNKNOWN, IDU_DEVICE_OFFLINE,
                         IDU_DEVICE_ONLINE, IDU_DEVICE_EXCEPTION };
struct idu_device_status {
    uint16_t dev_id;
    uint8_t bus_id;
    bool simulated;
    enum idu_device_online state;
    int error;
    uint32_t successes, failures;
    int64_t last_success;
};
struct idu_bus_cfg {
    uint8_t id;
    int iface;
    bool simulated;
    /* Optional protocol backend. In-place request/response; called under bus lock. */
    int (*transfer)(void *user, struct modbus_adu *adu);
    int (*initialize)(void *user, uint16_t dev_id);
    int (*read_model)(void *user, uint16_t dev_id, uint16_t model,
                       uint16_t offset, uint16_t *words, size_t count);
    void *user;
};
struct idu_collect_request {
    uint32_t id;
    uint16_t dev_id;
    uint8_t priority;
    enum idu_port_mode mode; /* Only scheduled while the port is in this mode. */
    struct modbus_adu adu;
};
typedef void (*idu_collect_done_fn)(const struct idu_collect_request *reply,
                                    int error, void *user);
int idu_collect_init_bus(const struct idu_equip_cfg *cfg, const struct idu_bus_cfg *bus);
int idu_collect_set_mode(uint8_t bus, enum idu_port_mode mode);
int idu_collect_get_mode(uint8_t bus, enum idu_port_mode *mode);
int idu_collect_enable_poll(size_t block, bool enabled);
int idu_collect_device_status(uint16_t dev_id, struct idu_device_status *status);
int idu_collect_initialize_device(uint16_t dev_id);
int idu_collect_submit(const struct idu_collect_request *request,
                        idu_collect_done_fn done, void *user);
int idu_collect_cancel(uint8_t bus); /* queued requests complete with -ECANCELED */
int idu_collect_transfer_mode(uint16_t dev_id, enum idu_port_mode mode, struct modbus_adu *adu);
/* Ordinary reads/writes require POLL, or the owning initialization context. */
int idu_collect_transfer(uint16_t dev_id, struct modbus_adu *adu);
int idu_collect_read(uint16_t dev_id, uint8_t fc, uint16_t address, uint16_t *words, size_t count);
int idu_collect_write(uint16_t dev_id, uint16_t address, const uint16_t *words, size_t count);
int idu_collect_reconfigure(uint8_t bus, struct modbus_iface_param param);
ZBUS_CHAN_DECLARE(idu_device_events);
#endif
