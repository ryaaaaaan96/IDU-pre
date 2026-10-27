# 工作队列监控验收

2026-10-27。本轮仅主机/native 模拟与固件构建，没有实板烧录。

## 实现

- 任务检查从单次 k_timer 改为系统 workqueue 的 delayable work；唯一硬件周期 k_timer 读取共享健康状态及有效期，操作 GPIO。
- 硬件周期仍由设备树配置；软件检查周期由 Kconfig 配置，默认 100 ms；有效期和启动宽限固定为三倍。
- 仅完整检查正常且旧结果未过期才续期，心跳不续期，队列恢复不清故障。
- 删除底层多实例、watchdog_error / watchdog_external_* / watchdog_task_monitor_*；使用统一初始化、停止、故障回调及必要快照。
- watchdog_reset 同步停止检查和喂狗，再调用 sys_reboot；status 仅保留产品故障策略、LED、协议/IAP 停止和镜像确认，删除硬件错误轮询工作。
- HOLD 显式关闭检查并允许继续喂狗，GPIO 失败不恢复输出，故障诊断保留。

## 验证结果

- `bash tests/task_watchdog/run_modes.sh`：双开与仅软件各 19 个独立进程；仅硬件/全关各配置检查与直接复位 2 个场景；初始化失败 1、首次检查宽限 1、软件周期 25/200 ms 各 3 个场景，共 50 个独立进程通过。
- 覆盖队列短暂阻塞、超过三倍周期停喂、队列恢复不能续期、持续心跳不能替检查续期、显式 HOLD 继续喂狗、关闭外部仍能直接复位。
- 硬件异常回调只在工作队列执行，ISR 只锁存并提交工作；首次检查未执行也有有限宽限。
- `tests/io/run.py`：4 个独立进程，覆盖显式启停、不可重新初始化、GPIO 错误锁存、初始化失败和非法参数，通过。
- acquisition：39 个独立进程通过，改为故障回调注入而非旧的错误轮询接口。
- runtime：正常 7、降级 1、debug 7、UART 初始化失败 1，共 16 个独立进程通过。
- `tests/test_build_configuration.py`：8 项通过。
- MCUboot + IAPv3 构建与验签通过；SGZ 152096 字节，镜像摘要 33f53fa7ff24aababfa288a5f4ed2cf59bf1d6158d061cb4b8820519251ab8c8。
- `git diff --check` 通过。

日志在工作区 build/watchdog-work-*.log、watchdog-*-tests.log 与 watchdog-period-*.log。
模块 reset 使用标准 CONFIG_REBOOT，由应用显式启用，不从依赖 GPIO 的选项 select REBOOT，以避免上游全局 Kconfig 依赖环。
硬件单实例接口替代旧多实例接口，IO 测试改为通过公开接口逐进程验证，完整 tests/run.sh 已接入新 runner。
未执行整个仓库的全部回归；本轮检查按监控/状态/硬件和固件范围选择。

## 限制

系统队列卡死期间故障通知也不能执行；带外部硬件时仍会按有效期停喂，由外部芯片兜底，只有软件时须等队列恢复。
任务扫描在工作队列的共享锁内执行，仍为 O(n)，实板最坏中断延迟须按任务规模测量。
实际复位还依赖硬件喂狗周期、芯片超时和引脚禁用电路；native 场景不替代真实波形及复位链验证。
