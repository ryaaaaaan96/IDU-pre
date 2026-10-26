# status 单文件内分段

根据用户澄清，撤回按文件拆分的组织方式，所有实现合回 app/status/status.c。
通过清晰的分段注释区分共享状态、LED、外部看门狗、任务期限监控以及整机状态/故障/健康检查。
删除 led.c、watchdog.c、task_monitor.c 和 internal.h，辅助函数恢复文件私有。

保留已经验证的简化：统一最早期限选择、统一喂狗错误处理、明确 STARTING -> RUN，
LED 不检查看门狗。合回后删除不再需要的跨文件停止、错误查询和 LED 起点包装接口。
对外接口和故障行为不变，文档与构建配置同步恢复单文件约定。

验证：任务监控 12、系统编排 39、runtime 16 个独立进程场景通过，共 67 场景。
MCUboot + IAPv3 固件构建和验签通过，SGZ 151872 字节。diff 空白检查通过。
未实板验证。日志位于工作区 build/status-sections-{watchdog,acquisition,runtime,firmware}.log。
