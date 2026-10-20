#include "hardware/idu_pre.h"
#include "process/engineering.h"
#include "legacy_registers.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

int main(void)
{
    struct idu_snapshot s = {.di = 0xa55a, .address = 16, .valid = true};
    uint16_t value;
    assert(idu_current_from_raw(0) == 0);
    assert(idu_current_from_raw(2048) == 34375);
    assert(idu_current_from_raw(4095) == UINT16_MAX);
    for (unsigned raw = 1; raw < 4096; ++raw) {
        assert(idu_current_from_raw(raw) >= idu_current_from_raw(raw - 1));
    }
    assert(idu_input_register_read(&s, 8149, &value) == 0 && value == 0x2221);
    assert(idu_input_register_read(&s, 8150, &value) == 0 && value == 0x0101);
    assert(idu_input_register_read(&s, 8151, &value) == 0 && value == 0x0100);
    assert(idu_input_register_read(&s, 8152, &value) == 0 && value == 0xa55a);
    for (unsigned i = 0; i < IDU_ANALOG_COUNT; ++i) {
        s.current[i] = i * 100;
        assert(idu_input_register_read(&s, 8163 + i, &value) == 0);
        assert(value == i * 100);
    }
    assert(idu_input_register_read(&s, 8162, &value) == 0 && value == 0);
    assert(idu_input_register_read(&s, 8171, &value) == 0 && value == 0);
    assert(idu_input_register_read(&s, 4814, &value) == 0 && value == 16);
    assert(idu_input_register_read(&s, 2504, &value) == 0 && value == 0x5347);
    assert(idu_input_register_read(&s, 2581, &value) == 0 && value == 0x4944);
    assert(idu_input_register_read(&s, 2595, &value) == 0 && value == 0);
    assert(idu_input_register_read(&s, 8200, &value) == -ENOTSUP);
    s.valid = false;
    assert(idu_input_register_read(&s, 8152, &value) == -EIO);
    assert(idu_input_register_read(&s, 8163, &value) == -EIO);
    assert(idu_input_register_read(&s, 8149, &value) == 0);
    puts("IDU-pre register and conversion tests passed");
    return 0;
}
