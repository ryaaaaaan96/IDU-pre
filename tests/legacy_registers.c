/* Frozen pre-point-table register behavior: regression oracle, not firmware. */
#include "legacy_registers.h"
#include <errno.h>

int idu_input_register_read(const struct idu_snapshot *s,
                            uint16_t address, uint16_t *value)
{
    static const uint8_t factory_sn[20] = "SG-FACTORY";
    static const char version[30] = "IDU-pre_0.1.0";
    if (address == 2503) {
        *value = 3;
    } else if ((address >= 2504 && address <= 2513) ||
               (address >= 4989 && address <= 4998) ||
               (address >= 4805 && address <= 4812)) {
        unsigned offset = address >= 4989 ? address - 4989 :
                          address >= 4805 ? address - 4805 : address - 2504;
        *value = ((uint16_t)factory_sn[offset * 2] << 8) | factory_sn[offset * 2 + 1];
    } else if (address >= 2581 && address <= 2595) {
        unsigned offset = (address - 2581) * 2;
        *value = ((uint16_t)(uint8_t)version[offset] << 8) | (uint8_t)version[offset + 1];
    } else if (address >= 2568 && address <= 2571) {
        const uint16_t confirmation[] = {0x3031, 0x4152, 3, 0x0103};
        *value = confirmation[address - 2568];
    } else if (address == 8149 || address == 4813) {
        *value = IDU_DEVICE_TYPE;
    } else if (address == 4814) {
        *value = s->address;
    } else if (address == 8150) {
        *value = IDU_PROTOCOL_VERSION & 0xffff;
    } else if (address == 8151) {
        *value = IDU_PROTOCOL_VERSION >> 16;
    } else if (address == 8152) {
        if (!s->valid) {
            return -EIO;
        }
        *value = s->di;
    } else if (address >= 8163 && address <= 8170) {
        if (!s->valid) {
            return -EIO;
        }
        *value = s->current[address - 8163];
    } else if (address == 5486) {
        *value = 0x1204;
    } else if ((address >= 2514 && address <= 2532) ||
               (address >= 4999 && address <= 5014) || address == 5487 ||
               (address >= 8153 && address <= 8162) ||
               (address >= 8171 && address <= 8199)) {
        *value = 0;
    } else {
        return -ENOTSUP;
    }
    return 0;
}
