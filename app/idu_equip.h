#ifndef IDU_EQUIP_H
#define IDU_EQUIP_H

#include "idu_point_service.h"
#include "signal_ids.h"
#include "device_config.h"
#include <dataprocess/points.h>
#include <dataprocess/params.h>
#include <collect/collect.h>
#include <transmit/transmit.h>

struct modbus_user_callbacks *idu_parameter_callbacks(void);
int idu_parameter_write(uint16_t address, const uint16_t *words, size_t count);

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
