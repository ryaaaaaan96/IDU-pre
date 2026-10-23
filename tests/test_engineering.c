#include "process/engineering.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(engineering_current_from_raw(0) == 0);
    assert(engineering_current_from_raw(2048) == 34375);
    assert(engineering_current_from_raw(4095) == UINT16_MAX);
    for (unsigned raw = 1; raw < 4096; ++raw) {
        assert(engineering_current_from_raw(raw) >= engineering_current_from_raw(raw - 1));
    }
    puts("IDU-pre engineering conversion tests passed");
    return 0;
}
