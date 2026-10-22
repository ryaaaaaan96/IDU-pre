#ifndef TEST_TR_TASKS_H
#define TEST_TR_TASKS_H

#include <transmit/modbus_adapter.h>
#include <zephyr/ztest.h>

#if defined(CONFIG_TR_MODBUS_ADAPTER)
static struct {
    bool bound;
    int iface;
    struct k_work_q queue;
} fixture_tr_ports[CONFIG_TR_PORTS];
static K_THREAD_STACK_ARRAY_DEFINE(fixture_tr_stacks, CONFIG_TR_PORTS, CONFIG_TR_STACK_SIZE);
static struct k_work_q fixture_tr_configuration;
static K_THREAD_STACK_DEFINE(fixture_tr_config_stack, CONFIG_TR_CONFIG_STACK_SIZE);

static inline void fixture_bind_tr(int iface)
{
    static bool started;
    if (!started) {
        k_work_queue_init(&fixture_tr_configuration);
        k_work_queue_start(&fixture_tr_configuration, fixture_tr_config_stack,
                           K_THREAD_STACK_SIZEOF(fixture_tr_config_stack), 8, NULL);
        started = true;
    }
    for (size_t i = 0; i < ARRAY_SIZE(fixture_tr_ports); ++i) {
        if (fixture_tr_ports[i].bound && fixture_tr_ports[i].iface == iface) { return; }
    }
    for (size_t i = 0; i < ARRAY_SIZE(fixture_tr_ports); ++i) {
        if (fixture_tr_ports[i].bound) { continue; }
        fixture_tr_ports[i].iface = iface;
        fixture_tr_ports[i].bound = true;
        k_work_queue_init(&fixture_tr_ports[i].queue);
        k_work_queue_start(&fixture_tr_ports[i].queue, fixture_tr_stacks[i],
                           K_THREAD_STACK_SIZEOF(fixture_tr_stacks[i]), 6, NULL);
        const struct tr_modbus_execution execution = {
            .receive = &fixture_tr_ports[i].queue,
            .configuration = &fixture_tr_configuration,
        };
        zassert_ok(tr_modbus_bind_execution(iface, &execution));
        return;
    }
    zassert_unreachable("Test must configure sufficient TR_PORTS");
}
#endif

#endif
