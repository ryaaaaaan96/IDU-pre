#ifndef IDU_PROTOCOL_H
#define IDU_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

struct modbus_iface_param;
struct modbus_user_callbacks;

/* Configure/service the northbound port from application-thread context. */
int idu_protocol_set_address(uint8_t address);

/* Product parameter commands and status registers for the Modbus adapter. */
struct modbus_user_callbacks *idu_parameter_callbacks(void);
int idu_parameter_write(uint16_t address, const uint16_t *words, size_t count);

/* Optional AIDC profile: translate stored parameters to Zephyr serial settings. */
int idu_aidc_serial(struct modbus_iface_param *param);

#endif
