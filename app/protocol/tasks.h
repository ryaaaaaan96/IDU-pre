#ifndef IDU_PROTOCOL_TASKS_H
#define IDU_PROTOCOL_TASKS_H

/* 创建为挂起态；本地装配完成后统一放行。仅供 protocol 内部编排。 */
int idu_dp_tasks_prepare(void);
void idu_dp_tasks_start(void);
int idu_co_tasks_prepare(void);
void idu_co_tasks_start(void);

int idu_start_forwarding(void);

#endif
