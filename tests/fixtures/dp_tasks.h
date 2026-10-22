#ifndef TEST_DP_TASKS_H
#define TEST_DP_TASKS_H

#include <dataprocess/points.h>
#if defined(CONFIG_DP_PROCESS)
#include <dataprocess/process.h>
#endif

static struct k_thread fixture_samples;
static K_THREAD_STACK_DEFINE(fixture_sample_stack, CONFIG_DP_POINTS_STACK_SIZE);
#if defined(CONFIG_DP_PROCESS)
static struct k_thread fixture_business;
static K_THREAD_STACK_DEFINE(fixture_business_stack, CONFIG_DP_PROCESS_STACK_SIZE);
#endif

static inline void fixture_start_dp(void)
{
    static bool started;
    if (started) { return; }
    started = true;
    k_thread_create(&fixture_samples, fixture_sample_stack, K_THREAD_STACK_SIZEOF(fixture_sample_stack),
                    dp_samples_task, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
#if defined(CONFIG_DP_PROCESS)
    k_thread_create(&fixture_business, fixture_business_stack, K_THREAD_STACK_SIZEOF(fixture_business_stack),
                    dp_business_task, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
#endif
}

#endif
