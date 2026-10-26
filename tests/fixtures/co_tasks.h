#ifndef TEST_CO_TASKS_H
#define TEST_CO_TASKS_H

#include <collect/collect.h>

static struct k_thread fixture_buses[CONFIG_CO_MAX_BUSES];
static K_THREAD_STACK_ARRAY_DEFINE(fixture_bus_stacks, CONFIG_CO_MAX_BUSES, CONFIG_CO_STACK_SIZE);

static inline void fixture_start_co(void)
{
    static bool started;
    if (started) { return; }
    started = true;
    for (size_t i = 0; i < ARRAY_SIZE(fixture_buses); ++i) {
        co_port_mode_t mode;
        if (co_get_mode(i, &mode)) { continue; }
        k_thread_create(&fixture_buses[i], fixture_bus_stacks[i],
                        K_THREAD_STACK_SIZEOF(fixture_bus_stacks[i]),
                        co_bus_task, (void *)(uintptr_t)i, NULL, NULL, 7, 0, K_NO_WAIT);
    }
}

#endif
