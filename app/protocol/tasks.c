#include "tasks.h"
#include <dataprocess/points.h>
#include <dataprocess/process.h>
#include <errno.h>

static struct k_thread samples, business;
K_THREAD_STACK_DEFINE(sample_stack, CONFIG_DP_POINTS_STACK_SIZE);
K_THREAD_STACK_DEFINE(business_stack, CONFIG_DP_PROCESS_STACK_SIZE);
static bool prepared;

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
