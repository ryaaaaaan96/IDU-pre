#ifndef IDU_ACQUISITION_SERVICE_H
#define IDU_ACQUISITION_SERVICE_H

#include "status/idu_status.h"

/* idu_startup() 成功后，由应用线程执行一个采样/发布/转发周期。
 * main 负责周期和休眠；故障保留最初阶段与错误，直到复位。
 */
int idu_acquisition_cycle(struct idu_status *status);

#endif
