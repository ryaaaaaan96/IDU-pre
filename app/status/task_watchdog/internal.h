#ifndef STATUS_TASK_WATCHDOG_INTERNAL_H
#define STATUS_TASK_WATCHDOG_INTERNAL_H

struct watchdog;
int status_tasks_init(struct watchdog *monitor);
void status_tasks_stop(struct watchdog *monitor);

#endif
