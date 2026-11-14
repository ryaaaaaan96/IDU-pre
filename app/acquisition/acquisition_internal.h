/** @file @brief 各类采集初始化与到期任务接口。 */
#ifndef ACQUISITION_INTERNAL_H
#define ACQUISITION_INTERNAL_H
#include "schedule.h"

int di_init(void);
int analog_init(void);
void analog_stop(void);
int fan_feedback_init(void);
void fan_feedback_stop(void);

/* 输出下一步动作的绝对毫秒时间；未到期不读取，停用时返回无限远。 */
int di_acquire(int64_t *next_deadline_ms);
int analog_acquire(int64_t *next_deadline_ms);
int fan_feedback_acquire(int64_t *next_deadline_ms);
#endif
