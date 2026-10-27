# 本地故障策略与诊断

其他模块只包含 `<watchdog/watchdog.h>`，上报非零错误码、常驻英文描述和最终动作，不依赖 status：

```c
watchdog_report_fault(err, "Input initialization failed", WATCHDOG_ACTION_HOLD);
watchdog_report_fault(err, "Input point update failed", WATCHDOG_ACTION_RESET);
```

描述保存指针，不复制或按字符串选择策略；应使用字符串常量或常驻存储。
status 根据接收故障时的整机状态选择启动或运行策略，不再单独保存故障阶段。
整机只维护未配置、启动、运行与故障锁存阶段。

| 动作 | 行为 |
| --- | --- |
| CONTINUE | 仅更新最近故障和提交日志，不恢复已经停止的业务 |
| HOLD | 锁存终止状态，停止业务与任务检查，显式保留外部喂狗 |
| RESET | 锁存终止状态，停止业务与全部监控，调用 watchdog_reset |

初始化阶段不允许 CONTINUE，传入时返回 -EINVAL。启动初始化失败默认 HOLD，运行故障默认 RESET，动作直接写在 C 调用点。
watchdog 在通知前更新最近故障快照，不判断重复、首次或升级，不维护历史。
watchdog_fault_get 统一返回最近主动上报或监控故障；action 只对主动上报有效，表示请求而非实际恢复状态。
参数无效或未绑定回调时不更新；合法请求即使被 status 的启动策略拒绝，也保留记录。
重复终止请求允许重复调用停止接口，业务停止标志复位前不清除。

## Watchdog 与产品处理

main 只调用 status_init；status 内部通过 watchdog_init(status_watchdog_fault) 绑定唯一故障回调并启动监控，然后初始化 LED。
watchdog 内部读取设备树并启动硬件喂狗及工作队列检查，不引用 status，也不提供多订阅者注册表。
watchdog_report_fault 在调用者上下文、模块锁外同步调用唯一 status 回调，并返回回调结果；main 不增加故障处理入口。
主动上报不改变 watchdog 的监控锁存故障，CONTINUE 不使监控停喂。
监控锁存用于健康门控和待通知事件，最近故障用于查询，两者均由 watchdog 内部管理；status 不再复制故障。
引脚和硬件周期来自 app,gpio-watchdog 节点，软件周期来自 WATCHDOG_CHECK_INTERVAL_MS。
首次检查宽限和健康结果有效期固定为三倍软件周期。只有完整检查通过才能续期，任务心跳不续期。

watchdog 在系统 workqueue 检查任务并回调故障。status 根据任务超时、检查结果过期、GPIO 错误选择产品动作。
硬件定时器只判断健康结果并操作 GPIO；队列卡死时过期后停喂，由外部芯片兜底，通知要等队列恢复。
过期故障不会在队列恢复时清除。HOLD 显式调用 watchdog_stop(MONITOR) 后允许喂狗，诊断仍保留。
仅软件配置没有外部兜底，队列阻塞期间不能主动执行该队列上的故障回调。

模块初始化错误直接返回，status 进入启动 HOLD，不重试初始化，也不再轮询未初始化硬件。
已成功启动的喂狗在启动 HOLD 中继续；硬件自身初始化失败时，不能保证外部芯片不会复位。
软件/硬件配置分别控制检查与 GPIO 操作，不改写调用点的显式故障动作。

## 显式复位与同步路径

watchdog_report_fault 可从线程或 ISR 调用；status 回调同步锁存故障并关闭业务准入。
RESET 先调用 watchdog_stop(ALL)，再关闭协议/IAP 准入，提交非阻塞日志并调用 watchdog_reset。
底层 sys_reboot 仅由 watchdog 调用，status 不直接接触复位原语；复位不等待 workqueue、日志或 LED。
测试允许复位入口返回，产品冷复位正常不返回。HOLD 保留系统队列中的 LED 更新。

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

LED 初始化失败终止启动；未成功配置时不访问 GPIO。运行态由独立 delayable work 更新，终止故障常亮。
系统队列拥堵可能延迟 LED，不能将灯的更新速度当作任务监控或停喂保证。

调试构建使用 `./scripts/build.sh --extra-conf "$PWD/conf/debug.conf"` 关闭软件/硬件监控，显式 RESET 仍有效。
验证使用 tests/runtime/run.sh、tests/acquisition/run.py、tests/task_watchdog/run_modes.sh；独立进程模拟每次启动。
当前模块契约见[任务检查与健康有效期](task-watchdog.md)。实板波形、时延和实际复位链另行验证。
