# 产品故障策略与状态指示

[文档导航](README.md) · [watchdog 契约](../modules/watchdog/README.md)

## 职责与初始化

main 调用 status_init，status 显式初始化 watchdog 并绑定唯一只读故障回调，再初始化 LED。
watchdog 不引用 status、协议或 IAP，应用不感知喂狗引脚。

watchdog 维护任务期限、健康结果、外部喂狗和最近故障；status 维护整机阶段与 LED。
主动上报可能从线程或 ISR 同步进入回调，因此回调须非阻塞、ISR 安全，不递归上报。
内部监控通知由系统工作队列执行，底层机制与完整 API 见模块 README。

## 终止故障约定

该通道用于本次启动不可恢复的故障。重试、远端离线等可恢复异常由业务模块管理。
其他模块直接上报错误、常驻英文描述和动作，不依赖 status：

```c
watchdog_report_fault(err, "Input initialization failed", WATCHDOG_ACTION_HOLD, 0);
watchdog_report_fault(err, "Input point update failed", WATCHDOG_ACTION_RESET, 200);
```

| 动作 | 产品状态与执行行为 |
| --- | --- |
| CONTINUE | 仅保留诊断兼容；不解除已有终止状态，新的产品终止故障不使用 |
| HOLD | status 锁存故障；watchdog 停止检查，GPIO 可用时继续喂狗，等待人工重启 |
| RESET | status 锁存故障；watchdog 按 uint32_t 毫秒延时安排冷复位 |

启动初始化失败由调用点选择 HOLD；采样失败选择 RESET/200 ms。
内部任务超时、健康过期及运行期喂狗 GPIO 故障统一 RESET/200 ms。
初始化错误直接返回，由应用上报，不根据“是否已登记任务”推测故障阶段。

延时仅 RESET 使用，0 表示立即复位，超过 60 秒按 60 秒提前。
重复请求只能提前期限；HOLD 不取消已安排的复位，GPIO 失败也不能靠切换模式恢复。
延时不保证日志发送或参数落盘，外部芯片可能提前触发复位。

## 业务停止与留驻

status 锁存 STATUS_FAULT_LOCKED，调用 iap_stop 拒绝新升级；协议线程继续排空端口队列，
只在 STATUS_RUN 时向 vport 提交业务。DP 本身不维护整机停止状态，RAM 读写与后台保存能力仍属于 DP。
已经开始的处理可能执行到边界，故障上报不撤销已完成副作用。

主循环停止采样后留驻，每 5 秒查询并打印最近故障，不重复上报、不恢复业务。
故障回调、ISR 和工作队列不进入打印死循环。RESET 的独立定时器仍可到期复位。
最近故障仅保存在 RAM，不表示故障消除，也不提供历史日志。

任务监控只覆盖显式登记任务；当前产品不自动登记主循环、DP、transmit 或 IAP。
即使没有活动任务，软件检查仍需运行并更新健康期限，以监控系统队列执行情况。
终止故障不能通过迟到心跳或后续 CONTINUE 清除。

## 状态与 LED

| 整机阶段 | 指示 |
| --- | --- |
| 未配置 | 尚未选择播放图案 |
| 启动中 | 亮/灭各 500 ms |
| 运行中 | 亮/灭各 100 ms |
| 故障锁存 | 常亮 |

status_led.c 使用每状态独立的循环步骤表，状态变化从第一步开始，同状态请求不重启。
时长为 0 的步骤保持电平。一个 delayable work 驱动 GPIO，队列迟到会拉长步骤，不跳过边沿。
灯光刷新不能作为健康或停喂保证。

startup 在必要初始化成功后确认 MCUboot 镜像，然后按当前代码等待 10 秒再进入运行状态；
确认失败进入启动 HOLD。镜像确认不验证后续运行稳定性。

## 调试与验证

conf/debug.conf 只关闭外部喂狗，软件检查和显式 RESET 仍有效。
暂停软件监控可显式调用 watchdog_stop(WATCHDOG_STOP_MONITOR)，本次启动不能重新启用，
也不取消已有复位。CPU halt 时 k_timer 无法继续提供外部喂狗边沿。

测试入口与当前版本验证范围见[验证说明](testing.md)。实板边沿、复位时延与队列阻塞行为须独立验收。
