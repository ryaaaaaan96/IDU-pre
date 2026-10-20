#ifndef IDU_PRE_H
#define IDU_PRE_H

#include <stdbool.h>
#include <stdint.h>

#define IDU_ANALOG_COUNT 8
#define IDU_DI_COUNT 16
#define IDU_DEVICE_TYPE 0x2221
#define IDU_PROTOCOL_VERSION 0x01000101U

struct idu_snapshot {
    uint16_t di;
    uint16_t raw[IDU_ANALOG_COUNT];
    uint16_t current[IDU_ANALOG_COUNT]; /* Legacy wire unit: 0.01 A */
    uint8_t address;
    bool valid;
};

int idu_io_init(void);
int idu_io_read(uint16_t *di, uint8_t *address);
int idu_led_toggle(void);
int idu_analog_init(void);
int idu_analog_read(uint16_t raw[IDU_ANALOG_COUNT]);
int idu_health_init(void);
void idu_health_kick(void);
void idu_health_stop(void);

#endif
