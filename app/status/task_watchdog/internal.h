#ifndef STATUS_TASK_WATCHDOG_INTERNAL_H
#define STATUS_TASK_WATCHDOG_INTERNAL_H

struct watchdog;
#include <stdbool.h>
int status_tasks_init(struct watchdog *monitor, void (*expired)(void));
void status_tasks_halt(struct watchdog *monitor, bool stop_feeding);

#endif
