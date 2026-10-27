# Watchdog 模块

实现集中在 `src/watchdog.c`，公开声明集中在 `include/watchdog/watchdog.h`。
应用不接触引脚、GPIO 实例或底层复位函数。模块不引用 status、协议或 IAP。

| 接口 | 作用 |
| --- | --- |
| `watchdog_init(notify)` | 每次上电只尝试一次，读取硬件配置并启动检查/喂狗 |
| `watchdog_stop(mode)` | MONITOR 停止检查但保留喂狗；ALL 同时停检查和喂狗 |
| `watchdog_reset()` | 同步停止全部监控和喂狗，调用 sys_reboot 冷复位 |
| `watchdog_task_register/feed/sleep_forever` | 显式登记、报告进展、退出任务期限监控 |
| `watchdog_task_get` / `watchdog_fault_get` | 复制任务或故障诊断，不改变状态 |

删除底层多实例和 watchdog_error / watchdog_external_* / watchdog_task_monitor_* 接口。
硬件错误、任务超时和检查结果过期都通过一个故障回调报告，初始化错误直接返回。
调用复位接口需应用启用 Zephyr CONFIG_REBOOT；关闭软件/外部开关不抑制显式复位。

## 检查、有效期与硬件喂狗

- 任务检查由系统 workqueue 的 delayable work 周期执行，不创建额外线程。
- 检查周期来自 CONFIG_WATCHDOG_CHECK_INTERVAL_MS，默认 100 ms；完成检查后安排下一个周期。
- 首次检查的启动宽限与每次成功检查的有效期都固定为三倍检查周期，默认 300 ms。
- 只有完整检查通过才能续期；登记、心跳和工作开始均不续期。
- 工作队列执行前先验证旧结果是否已过期；过期不能通过恢复执行自动改为健康。
- 硬件使用唯一一个周期 k_timer，只检查共享健康状态、有效期并执行 GPIO 翻转。
- 结果过期时，定时器锁存 CHECK 故障并停止发出边沿，只提交工作，不在 ISR 中回调应用。
- 无活动任务时仍周期检查工作队列运行情况并续期；永久休眠仅移出任务列表。

软件检查、GPIO 翻转和诊断快照通过短小自旋锁保护，64 位有效期与标志一致读写。
任务扫描在工作队列中执行，锁内扫描仍为 O(n)，大量任务时需实测其对中断延迟的影响。
没有堆分配、固定任务槽位或按到期时间重排的链表。名称和任务上下文由调用方常驻持有。
迟到心跳或退出返回 -ETIMEDOUT 并锁存异常，由工作队列通知，不能掩盖已经发生的任务超时。

## 设备树与配置

```dts
external_watchdog: external-watchdog {
    compatible = "app,gpio-watchdog";
    feed-gpios = <&gpioc 13 GPIO_ACTIVE_HIGH>;
    feed-interval-ms = <10>;
    status = "okay";
};
```

绑定由模块 dts_root 提供，外部启用时必须恰好有一个启用节点，周期大于零。
GPIO 必须是支持 ISR 安全访问的片上 GPIO。由 main → status_init → watchdog_init 显式启动，不注册 SYS_INIT。

| 软件 | 外部 | 行为 |
| --- | --- | --- |
| y | y | 工作队列检查，硬件依据健康及有效期喂狗 |
| y | n | 工作队列检查；队列阻塞时没有外部硬件兜底，只能在恢复后发现过期 |
| n | y | 无软件有效期限制，硬件周期喂狗，GPIO 错误仍在工作队列通知 |
| n | n | 不启动检查或喂狗，显式复位仍可调用 |

硬件周期只在设备树配置，软件周期只在 Kconfig 配置，有效期不单独配置。
停止边沿最晚需要等待下一个硬件定时器回调；实际复位还需外部芯片自身的超时时间。
系统队列中合法的长任务同样可能消耗有效期，应通过软件检查周期覆盖允许的最长阻塞。

## 故障与应用动作

任务超时、检查过期锁存后拒绝后续心跳，不因工作队列恢复或新心跳自动复原。
故障回调只在系统 workqueue、模块锁外执行，快照只在回调期间有效；初始化失败不重复通知。
新的 GPIO 故障可替换此前的监控故障，用于应用已留驻后硬件喂狗失败的升级处理。

模块只报告事实。应用选择 HOLD 时显式 stop(MONITOR)，停止检查并允许继续喂狗，但不清除诊断；
选择 RESET 时 stop(ALL) 并调用 watchdog_reset。后者直接调用底层冷复位，不等待系统队列、日志或显示。
工作队列卡死时，故障通知不能运行；硬件定时器仍会按三倍有效期停喂，由外部芯片兜底复位。
