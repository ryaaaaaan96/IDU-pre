#include "process/idu_process.h"
#include "acquisition/idu_acquisition.h"
#include <zephyr/kernel.h>

int main(void)
{
    struct idu_acquisition acquisition = {0};
    int err = idu_acquisition_start(&acquisition);
    if (err) { return err; }

    while (true) {
        int64_t next_sample = k_uptime_get() + idu_sample_period_ms();
        err = idu_acquisition_cycle(&acquisition);
        if (err) { return err; }
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
}
