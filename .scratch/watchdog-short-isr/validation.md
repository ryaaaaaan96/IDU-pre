# 两个定时器与短小回调验收

2026-10-27。仅本地主机/native 模拟和固件构建，无实板烧录。

## 修改

- 保留单 watchdog.c/公开头、两个 k_timer 和显式初始化，不新增线程或自动任务登记。
- 活动任务在线程接口中按绝对期限排序。模块定时回调仅检查表头并锁存状态；线程重排仍有 O(n) 锁内开销。
- RESET 优先停止外部喂狗，再关闭协议/IAP 准入；复位先于 LED/辅助工作提交，不等待工作队列。
- 使用 Zephyr 延后、非阻塞日志；构建拒绝同步日志或在线程等待日志缓冲的配置。不新建应用日志队列。

## 验证

- `bash tests/task_watchdog/run_modes.sh`：双开、仅软件各 15 个独立进程；仅硬件、全关各 1 个配置场景；GPIO 初始化失败 1 个场景，全部通过。
- 新增两个行为场景：系统 workqueue 阻塞时，硬件继续喂狗、任务超时仍从 ISR 执行关键门控/停喂/复位；显式 ISR HOLD 仍关闭业务并继续喂狗。
- 复位钩子检查协议停止时硬件已停喂；原有独立期限、休眠/恢复、并发快照场景通过。
- acquisition：39 个独立进程通过。
- `tests/runtime/run.sh`：正常 7、降级 1、debug 7、UART 初始化失败 1，共 16 个独立进程通过。
- 负向配置：LOG_MODE_IMMEDIATE 和 LOG_BLOCK_IN_THREAD 各自被 status 构建约束拒绝，符合预期。
- `scripts/build.sh`：MCUboot + IAPv3 编译、验签通过；SGZ 152240 字节，镜像摘要 26791f1dea84c37c2696c0551120778247246399d6c77d68c6d7e2d5ff27869e。
- `git diff --check` 通过。

首次原生回归暴露默认同步日志与新契约冲突，已统一使用延后模式。
Zephyr 延后日志默认也捕获 printk，可能在测试进程退出前遗漏列表/复位标记；夹具显式 LOG_PRINTK=n，保证测试列表和验收标记立即输出。
修正后完整核对上述场景数量，不将输出不全时的早期运行计入通过结果。

日志：工作区 build/watchdog-isr-*.log、watchdog-both-tests.log、watchdog-software-tests.log 等。
原生验证不代替实板 ISR 最坏耗时、锁内重排延迟、GPIO 波形及实际复位链测试。
