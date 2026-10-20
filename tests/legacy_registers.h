#ifndef IDU_LEGACY_REGISTERS_H
#define IDU_LEGACY_REGISTERS_H
#include "acquisition/inputs.h"
int idu_input_register_read(const struct idu_snapshot *snapshot,
                            uint16_t address, uint16_t *value);
#endif
