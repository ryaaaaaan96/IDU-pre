/** @file @brief 本地采集任务的初始化与启动。 */
#ifndef ACQUISITION_H
#define ACQUISITION_H

/**
 * @brief 初始化各类采集、创建并启动采样线程；不提供单独的 start 接口。
 * @note 启动阶段先完成 DP、转发和协议初始化，再串行调用一次本接口。
 * @note STATUS_STARTING 期间线程等待；进入 STATUS_RUN 后才采样，终止故障后不恢复。
 * @retval -EALREADY 本次启动已经成功初始化过，故障后也不能再次启动。
 * @retval -EACCES 整机尚未初始化或已进入终止故障。
 * @note 硬件初始化失败不创建线程，调用方进入 status 的终止诊断。
 */
int acquisition_init(void);

#endif
