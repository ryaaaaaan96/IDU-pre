#ifndef IDU_EQUIP_H
#define IDU_EQUIP_H

#include <idu/points.h>
#include <idu/params.h>

#define IDU_DEV_LOCAL 1
#define IDU_DEV_REMOTE_IO 10
#define IDU_SIG_DI 8152
#define IDU_SIG_CURRENT_1 8163
#define IDU_SIG_PROTOCOL 8150
#define IDU_SIG_ADDRESS 30459
#define IDU_SIG_SAVED_ADDRESS 30461 /* Internal key; legacy wire address is 30459. */
#define IDU_SIG_VIRTUAL_MODE 10012
#define IDU_SIG_SAMPLE_PERIOD 31000
#define IDU_SIG_LABEL 31001
#define IDU_SIG_REMOTE_SETPOINT 31500
#define IDU_SIG_REMOTE_OFFSET 31501

struct modbus_user_callbacks *idu_parameter_callbacks(void);
int idu_parameter_write(uint16_t address, const uint16_t *words, size_t count);
uint16_t idu_sample_period_ms(void);

const struct idu_equip_cfg *idu_equipment_config(void);
const struct idu_point_cfg *idu_process_table(size_t *count);
const struct idu_collect_cfg *idu_collect_table(size_t *count);
const struct idu_transmit_cfg *idu_transmit_table(size_t *count);
int idu_equipment_init(void);
int idu_acquisition_port_init(void); /* Returns Zephyr Modbus client iface. */

#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
enum idu_virtual_write_fault {
    IDU_VIRTUAL_WRITE_OK,
    IDU_VIRTUAL_WRITE_REJECT,
    IDU_VIRTUAL_WRITE_LOSE_ACK,
    IDU_VIRTUAL_WRITE_DISCONNECT,
};
void idu_virtual_set_write_fault(enum idu_virtual_write_fault fault);
unsigned idu_virtual_write_count(void);
int idu_virtual_port_init(void);
void idu_virtual_set_online(bool online);
int idu_virtual_set_register(uint8_t function, uint16_t address, uint16_t value);
#endif

#endif
