#include "tasks.h"
#include "inc/protocol.h"
#include <collect/collect.h>
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <errno.h>
#include <transmit/forward.h>
#include <transmit/modbus.h>
#include <zephyr/sys/atomic.h>

static struct k_thread samples, business;
K_THREAD_STACK_DEFINE(sample_stack, CONFIG_DP_POINTS_STACK_SIZE);
K_THREAD_STACK_DEFINE(business_stack, CONFIG_DP_PROCESS_STACK_SIZE);
static bool prepared;
static struct k_thread collect;
K_THREAD_STACK_DEFINE(collect_stack, CONFIG_CO_STACK_SIZE);
static bool collect_prepared;
static atomic_t protocol_error;

int idu_protocol_error(void)
{
    return (int)atomic_get(&protocol_error);
}

int idu_dp_tasks_prepare(void)
{
    if (prepared) { return -EALREADY; }
    k_thread_create(&samples, sample_stack, K_THREAD_STACK_SIZEOF(sample_stack),
                    dp_samples_task, NULL, NULL, NULL, 5, 0, K_FOREVER);
    k_thread_create(&business, business_stack, K_THREAD_STACK_SIZEOF(business_stack),
                    dp_business_task, NULL, NULL, NULL, 6, 0, K_FOREVER);
    prepared = true;
    return 0;
}

void idu_dp_tasks_start(void)
{
    if (!prepared) { return; }
    k_thread_start(&samples);
    k_thread_start(&business);
}

int idu_co_tasks_prepare(void)
{
    if (collect_prepared) { return -EALREADY; }
    k_thread_create(&collect, collect_stack, K_THREAD_STACK_SIZEOF(collect_stack),
                    co_bus_task, (void *)(uintptr_t)idu_collect_bus.id,
                    NULL, NULL, 7, 0, K_FOREVER);
    collect_prepared = true;
    return 0;
}

void idu_co_tasks_start(void)
{
    if (collect_prepared) { k_thread_start(&collect); }
}

#if defined(CONFIG_TR_FORWARD_CONFIG)
static struct k_work_q receive_queue, configuration_queue;
K_THREAD_STACK_DEFINE(receive_stack, CONFIG_TR_STACK_SIZE);
K_THREAD_STACK_DEFINE(configuration_stack, CONFIG_TR_CONFIG_STACK_SIZE);
static struct k_work_delayable maintenance;
static bool forwarding_started;
static bool forwarding_prepared;

static void maintain_forwarding(struct k_work *work)
{
    ARG_UNUSED(work);
    int err = tr_forward_service(&idu_forwarding, CONFIG_IDU_FORWARD_UNIT);
    if (err) { atomic_cas(&protocol_error, 0, err); }
    k_work_reschedule_for_queue(&configuration_queue, &maintenance,
                                K_MSEC(CONFIG_IDU_PROTOCOL_SERVICE_PERIOD_MS));
}

int idu_start_forwarding(void)
{
    if (forwarding_started) { return -EALREADY; }
    const char *name = tr_modbus_port_name(idu_forwarding.port_id);
    if (!name) { return -ENOTCONN; }
    int iface = modbus_iface_get_by_name(name);
    if (iface < 0) { return iface; }
    if (!forwarding_prepared) {
        k_work_queue_init(&receive_queue);
        k_work_queue_init(&configuration_queue);
        k_work_queue_start(&receive_queue, receive_stack, K_THREAD_STACK_SIZEOF(receive_stack), 6, NULL);
        k_work_queue_start(&configuration_queue, configuration_stack,
                           K_THREAD_STACK_SIZEOF(configuration_stack), 8, NULL);
        forwarding_prepared = true;
    }
    const struct tr_modbus_execution execution = {
        .receive = &receive_queue,
        .configuration = &configuration_queue,
    };
    int err = tr_modbus_bind_execution(iface, &execution);
    if (!err) { err = tr_forward_init(&idu_forwarding, CONFIG_IDU_FORWARD_UNIT); }
    if (err) { return err; }
    forwarding_started = true;
    k_work_init_delayable(&maintenance, maintain_forwarding);
    k_work_reschedule_for_queue(&configuration_queue, &maintenance,
                                K_MSEC(CONFIG_IDU_PROTOCOL_SERVICE_PERIOD_MS));
    return 0;
}
#endif
