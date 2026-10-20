#include "idu_equipment.h"
#include "idu_runtime.h"
#include <zephyr/kernel.h>

int main(void)
{
    struct idu_runtime runtime = {0};
    int err = idu_runtime_init(&runtime);
    if (err) { return err; }

    while (true) {
        int64_t next_sample = k_uptime_get() + idu_sample_period_ms();
        err = idu_runtime_step(&runtime);
        if (err) { return err; }
        int64_t delay = next_sample - k_uptime_get();
        if (delay > 0) { k_sleep(K_MSEC(delay)); }
    }
}
