# 本地故障策略与诊断

> status 锁存 `STATUS_FAULT_LOCKED`，主循环据此停止业务并保留诊断轮询。
> 可选 IAP 通过 `iap_stop()` 停止；DP 只负责数据，不维护整机停止状态。

产品约定：watchdog → status 仅接收本次启动不可恢复的终止故障，调用方选择 HOLD（等待人工重启）或 RESET（自动重启）。
故障原因消失不解除锁存，不提供现场业务恢复；可恢复异常由业务模块自行处理，不进入该通道。重启不保证排除实际故障原因。
现有 CONTINUE 保留原诊断行为，不属于终止故障约定，新的产品故障调用不使用它；本次文档修订不改变其运行行为。

其他模块只包含 `<watchdog/watchdog.h>`，上报非零错误码、常驻英文描述和最终动作，不依赖 status：

```c
watchdog_report_fault(err, "Input initialization failed", WATCHDOG_ACTION_HOLD, 0);
watchdog_report_fault(err, "Input point update failed", WATCHDOG_ACTION_RESET, 200);
```

描述保存指针，不复制或按字符串选择策略；应使用字符串常量或常驻存储。
status 按接收故障时的整机状态锁存阶段、关闭业务并提交日志，不再选择或改写动作。
整机只维护未配置、启动、运行与故障锁存阶段。

| 动作 | 行为 |
| --- | --- |
| CONTINUE | 仅更新最近故障和提交日志，不恢复已经停止的业务 |
| HOLD | 锁存终止状态，停止业务与任务检查，显式保留外部喂狗 |
| RESET | 回调锁存终止状态、停止业务并提交日志；watchdog 使用调用方传入的 reset_delay_ms 调用私有复位逻辑，到期停喂并复位 |

初始化阶段不允许 CONTINUE，传入时返回 -EINVAL。启动初始化失败默认 HOLD，运行故障默认 RESET，动作直接写在 C 调用点。
watchdog 在通知前更新最近故障快照，不判断重复、首次或升级，不维护历史。
watchdog_fault_get 统一返回最近主动上报或监控故障；action 表示上报请求或 watchdog 内部默认动作，不代表实际恢复状态。
参数无效或未绑定回调时不更新；合法请求会保留记录，回调只读，不能拒绝或改写动作。
终止动作允许重复上报，业务停止标志在复位前不清除。

## Watchdog 与产品处理

main 只调用 status_init；status 内部通过 watchdog_init(status_watchdog_fault) 绑定唯一故障回调并启动监控，然后初始化 LED。
watchdog 内部读取设备树并启动硬件喂狗及工作队列检查，不引用 status，也不提供多订阅者注册表。
watchdog_report_fault 在调用者上下文、模块锁外同步调用唯一 status 回调，随后 watchdog 按 action 执行动作，再返回回调结果；main 不增加故障处理入口。
主动上报不改变 watchdog 的监控锁存故障，CONTINUE 不使监控停喂。
单一原子模式负责健康门控，故障队列保存待通知事件，一份最近诊断用于查询；status 不复制故障。
引脚和硬件周期来自 app,gpio-watchdog 节点，软件周期来自 WATCHDOG_CHECK_INTERVAL_MS。
首次检查宽限和健康结果有效期固定为三倍软件周期。只有完整检查通过才能续期，任务心跳不续期。

watchdog 在系统 workqueue 检查任务并回调故障。status 只观察任务超时、检查结果过期、GPIO 错误并记录终止状态，不再选择动作。
内部监控故障在首个任务成功登记前按 HOLD 处理，登记后按 RESET/200 ms；主循环不登记自身，未登记任何任务时内部监控故障保持 HOLD。
硬件定时器只判断健康结果并操作 GPIO；队列卡死时过期后停喂，由外部芯片兜底，通知要等队列恢复。
过期故障不会在队列恢复时清除。HOLD 由 watchdog 统一调用 watchdog_stop(MONITOR) 后允许喂狗，诊断仍保留。
仅软件配置没有外部兜底，队列阻塞期间不能主动执行该队列上的故障回调。

模块初始化错误直接返回，status 进入启动 HOLD，不重试初始化，也不再轮询未初始化硬件。
已成功启动的喂狗在启动 HOLD 中继续；硬件自身初始化失败时，不能保证外部芯片不会复位。
软件检查始终随模块初始化运行；外部喂狗配置仅控制 GPIO 操作，不改写调用点的显式故障动作。

## 显式复位与延时路径

watchdog_report_fault 可从线程或 ISR 调用；status 回调同步锁存故障并关闭业务准入。
业务上报和内部监控故障统一经过 watchdog 的 handle_fault：先调用只读 status 回调，由其锁存状态、关闭协议/IAP 准入并提交非阻塞日志，再由 watchdog 按快照中的 action 执行最终动作。
回调不得修改快照；HOLD 由 watchdog 调用 watchdog_stop(MONITOR)，RESET 由 watchdog 使用调用方传入的 reset_delay_ms 调用私有复位逻辑。status 不直接调用这两个接口。
watchdog_report_fault 的第四参数 reset_delay_ms 是 uint32_t 毫秒，仅动作为 RESET 时有效；其他动作忽略。复位函数不公开；非零时安排一次性 k_timer 并立即返回，延时期间保留可用硬件喂狗，到期在中断中原子停喂并取消检查，再执行 sys_reboot 冷复位，不获取模块锁。
0 表示立即复位；重复非零请求只允许提前，不推迟已安排的期限。延时使用相对超时，上限 60 秒；更长的请求按 60 秒提前。
底层 sys_reboot 由 watchdog 复位路径调用，status 不直接接触复位原语；复位不等待 workqueue、日志或 LED。
延时只是提供输出机会，不保证日志已打印或参数已保存；GPIO 已失败/喂狗已停止时，外部芯片可能更早复位。HOLD 和复位等待期间保留后台 LED 更新。

开启日志时要求 LOG_MODE_DEFERRED 并关闭 LOG_BLOCK_IN_THREAD，构建拒绝同步日志或等待日志缓冲的配置。
每次报告仍提交日志，不增加应用日志队列、不等待复位前刷出，日志落盘不属于复位前保证。

终止时 DP/CO 拒绝新业务，北向静默，IAP 拒绝新上传。已开始的通信和 Flash 操作到安全边界后停止；
不挂起持锁线程、不撤销已执行的南向命令或升级提交。故障路径不写 Flash，诊断不跨重启保存。
详细决策历史见 [ADR 0011](adr/0011-terminal-faults-and-runtime-budgets.md)。

## 产品状态与指示

status 只维护整机阶段、产品故障动作和 LED，不保存故障或远端/存储诊断副本。
远端状态直接调用 co_device_status，参数状态直接调用 dp_params_get_status；删除 protocol_update_status 的重复汇总。
远端离线或允许的存储降级不停止本地采样，不主动停止硬件喂狗。
startup 在必要初始化成功后直接调用 boot_write_img_confirmed，确认失败按初始化 HOLD 处理。
不再保留 image_confirm.c、status_progress 或运行健康计时；存储降级和远端离线不阻止镜像确认。

LED 初始化失败终止启动；未成功配置时不访问 GPIO。`status.c` 将整机阶段映射为 LED 指示状态，`status_led.c` 为每个指示状态保存独立的循环步骤：

| 阶段 | 显示 |
| --- | --- |
| 启动中 | 亮/灭各 500 ms |
| 运行中 | 亮/灭各 100 ms |
| 终止故障 | 常亮 |

指示状态变化时从第一步立即刷新，同状态重复请求不重启。每一步明确亮灭电平及持续毫秒数，最后一步后回到第一步；时长 0 表示保持而不再调度。当前仅保留初始化、运行、故障三个指示状态；未初始化时不选择图案。实际输出由一个 delayable work 调度，队列延迟会拉长图案，但不跳过步骤。
系统队列拥堵可能延迟 LED，不能将灯的更新速度当作任务监控或停喂保证。

调试构建使用 `./scripts/build.sh --extra-conf "$PWD/conf/debug.conf"` 只关闭外部喂狗；软件监控及显式 RESET 仍有效。需要暂停监控时显式调用 watchdog_stop(WATCHDOG_STOP_MONITOR)，本次启动不可重新启用，也不会取消待执行复位。
验证使用 tests/runtime/run.sh、tests/acquisition/run.py、tests/task_watchdog/run_modes.sh；独立进程模拟每次启动。
当前模块契约见[任务检查与健康有效期](task-watchdog.md)。实板波形、时延和实际复位链另行验证。

内部监控故障不推断产品启动阶段，统一请求 RESET/200 ms。硬件/业务初始化函数返回的错误仍由调用者主动选择 HOLD。
HOLD 和等待复位期间不再检查或续期；这两种状态的喂狗许可是显式策略，不表示业务健康。GPIO 失败不会被 HOLD 或复位等待恢复，STOP_ALL 后不因 HOLD 重新喂狗，HOLD 不取消已安排的复位。
故障查询不处理状态或消费通知；后来上报的 CONTINUE 可以更新诊断，但不能解除故障模式。显式 HOLD 后未投递的旧任务/CHECK 事件不再触发复位，新 GPIO 故障仍可升级。

## 主线程故障留驻与定期诊断

主线程启动失败、采样/维护返回错误或观察到整机已离开运行状态后，退出正常业务循环，进入故障留驻循环。立即查询 watchdog_fault_get 并打印最近故障，然后每 5 秒打印一次；无故障快照时打印停止时的返回错误。不重复上报故障、不喂任务心跳、不清除锁存，也不继续采样或通信维护。

只有主线程执行周期诊断，不新增故障打印线程或 work。watchdog_report_fault 和 status 回调处理后返回，不在回调、系统 workqueue 或 ISR 中循环。status 首次故障日志仍保留，后续主线程输出只是诊断查询，不触发动作。

HOLD 等待人工重启，后台 LED 和可用的外部喂狗继续运行；RESET 使用同一留驻路径等待，独立复位定时器到期仍可在主线程休眠期间复位。周期打印不保证日志已实际输出，也不作为复位前的完成条件。主线程正在执行的采样或周期休眠结束后才进入留驻，不增加独立通知机制。
