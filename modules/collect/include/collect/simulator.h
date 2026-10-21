#ifndef CO_SIMULATOR_H
#define CO_SIMULATOR_H
#include <collect/collect.h>

enum co_simulator_fault {
    CO_SIMULATOR_WRITE_OK, CO_SIMULATOR_WRITE_REJECT,
    CO_SIMULATOR_WRITE_LOSE_ACK, CO_SIMULATOR_WRITE_DISCONNECT,
};
struct co_simulator_register {
    uint8_t function, width; /* width 1/2 为一个不可拆分的值，低字在前。 */
    uint16_t address;
    uint32_t initial;
    bool writable, signed_value, unit_address;
    int64_t minimum, maximum;
};
struct co_simulator_config {
    const char *client_iface, *server_iface;
    uint16_t dev_id;
    uint32_t timeout_us;
    const struct co_simulator_register *registers;
    size_t register_count;
};
struct co_simulator {
    const struct co_simulator_config *config;
    struct k_mutex lock;
    int client, server;
    atomic_t online, fault, writes;
    uint16_t values[CONFIG_CO_SIMULATOR_WORDS];
    bool initialized, started;
};

/* 调用者持有零初始化实例和常驻配置；启动串行化，运行期读写受互斥锁保护。
 * 这是完整 ADU 回环，不模拟 UART 电气时序。拒绝拆分值写入，整包校验后提交。
 */
int co_simulator_start(struct co_simulator *sim, const struct co_simulator_config *config);
void co_simulator_set_online(struct co_simulator *sim, bool online);
void co_simulator_set_write_fault(struct co_simulator *sim, enum co_simulator_fault fault);
unsigned co_simulator_write_count(struct co_simulator *sim);
int co_simulator_set_register(struct co_simulator *sim, uint8_t function,
                               uint16_t address, uint16_t value);
#ifdef CONFIG_CO_MODBUS_RAW_PORT
#include <vport/message.h>
int co_simulator_port_send(struct co_simulator *sim, uint16_t destination,
                           const struct vport_source *source, const struct vport_frame *frame,
                           bool request);
#endif
#endif
