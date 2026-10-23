#ifndef PROTOCOL_TASKS_H
#define PROTOCOL_TASKS_H

/* 创建为挂起态；本地装配完成后统一放行。仅供 protocol 内部编排。 */
int protocol_dp_tasks_init(void);
void protocol_dp_tasks_start(void);
int protocol_co_tasks_init(void);
void protocol_co_tasks_start(void);

int protocol_forwarding_init(void);

#endif
