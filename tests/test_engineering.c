#include "process/engineering.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(idu_current_from_raw(0) == 0);
    assert(idu_current_from_raw(2048) == 34375);
    assert(idu_current_from_raw(4095) == UINT16_MAX);
    for (unsigned raw = 1; raw < 4096; ++raw) {
        assert(idu_current_from_raw(raw) >= idu_current_from_raw(raw - 1));
    }
    puts("IDU-pre engineering conversion tests passed");
    return 0;
}
