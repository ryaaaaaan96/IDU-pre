#ifndef ACQUISITION_H
#define ACQUISITION_H

#include "status/status.h"

/* application_init() 成功后，由应用线程执行一个采样/发布/转发周期。
 * main 负责周期和休眠；故障保留最初阶段与错误，直到复位。
 */
int acquisition_cycle(struct application_status *status);

#endif
