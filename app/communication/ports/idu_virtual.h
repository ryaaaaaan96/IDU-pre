#ifndef IDU_VIRTUAL_H
#define IDU_VIRTUAL_H

#include <stdbool.h>
#include <stdint.h>

struct dp_equip_cfg;

#if defined(CONFIG_IDU_COLLECT_VIRTUAL)
/* MV-IO fixture at the Modbus ADU boundary, without UART timing emulation. */
int idu_virtual_port_init(void);
void idu_virtual_set_online(bool online);
int idu_virtual_set_register(uint8_t function, uint16_t address, uint16_t value);

enum idu_virtual_write_fault {
    IDU_VIRTUAL_WRITE_OK,
    IDU_VIRTUAL_WRITE_REJECT,
    IDU_VIRTUAL_WRITE_LOSE_ACK,
    IDU_VIRTUAL_WRITE_DISCONNECT,
};
void idu_virtual_set_write_fault(enum idu_virtual_write_fault fault);
unsigned idu_virtual_write_count(void);
#endif

/* Optional AIDC profile: register a virtual model backend, not a UPP transport. */
int idu_aidc_bus_init(const struct dp_equip_cfg *cfg);

#endif
