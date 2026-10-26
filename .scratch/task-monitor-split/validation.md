# 独立任务期限监控

按用户要求将 status.c 的任务期限监控段移到根目录 task_monitor.c，task_monitor.h
为内部协作头，公开接口仍统一声明在 inc/status.h。
任务链表、超时快照和单次内核定时器由 task_monitor.c 私有持有；共享阶段及状态锁，
锁内停止期限定时器，锁外报告故障，使用同一个外部看门狗实例查询状态。
更新 CMake、文档与布局约定，不新增公开接口或任务登记。

验证：任务监控 12、系统编排 39、runtime 16 个独立进程场景通过，共 67 场景。
MCUboot + IAPv3 板级固件编译和验签通过，SGZ 152064 字节。diff 空白检查通过。
未实板验证。日志位于工作区 build/task-monitor-{split,acquisition,runtime,firmware}.log。
