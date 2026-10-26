# 本地故障策略与诊断

故障上报显式提供阶段、错误码、常驻描述和最终动作：

```c
status_report_fault(STATUS_INIT, err, "Input initialization failed",
                    STATUS_ACTION_HOLD);
status_report_fault(STATUS_RUNTIME, err, "Watchdog feed output failed", STATUS_ACTION_RESET);
```

`STATUS_INIT` / `STATUS_RUNTIME` 表示故障发生于初始化或运行阶段，运行时不再设置采样、发布等细分业务阶段。错误码表示失败原因，描述说明故障来源；同一个 `-EIO` 可来自不同硬件。
描述使用英文，必须是字符串常量或始终有效的常驻字符串，函数保存指针，不复制或动态格式化。
`status` 不比较描述字符串、不维护故障来源枚举或动作映射表，直接执行调用方的最终动作。

动作直接在 C 调用点使用 `status_action_t` 的枚举值：

- `STATUS_ACTION_CONTINUE`：记录后继续，仅运行期可用。
- `STATUS_ACTION_HOLD`：停止业务并留驻。
- `STATUS_ACTION_RESET`：请求复位。

启动初始化失败明确使用 `STATUS_ACTION_HOLD`，停止业务并保留故障现场，避免固定故障循环重启。
运行期故障继续使用 `STATUS_ACTION_RESET`；需要改变某处策略时直接修改该处枚举。
看门狗初始化成功后，启动故障留驻仍持续喂狗；看门狗自身初始化失败时不再检查未初始化实例，
也不请求软件复位，但无法保证外部硬件看门狗不会复位。
不通过 Kconfig 数字配置动作。`APP_DEBUG_SKIP_WATCHDOG` 只关闭看门狗，
不改变故障动作。初始化阶段传入 `STATUS_ACTION_CONTINUE` 返回 `-EINVAL`。

每次上报都更新描述、阶段、错误、时间和请求动作，并输出日志；快照表示最近一次故障，
不保存历史，也不判断是否重复。删除独立的终止描述和动作强度比较。
动作直接执行：继续只记录，留驻停止业务，复位停止业务后请求复位。
业务停止状态独立锁存，后续继续动作不会恢复业务；重复终止请求允许重复调用停止接口。

终止时 DP/CO 拒绝新业务，北向静默，IAP 拒绝新上传；保留 Shell、日志和 RAM 快照。
普通留驻继续喂外狗，调试留驻不启动看门狗。已经开始的操作到安全边界后停止，
不能撤销已执行的南向命令或升级提交，详细边界见 [ADR 0011](adr/0011-terminal-faults-and-runtime-budgets.md)。

`status.c` 私有持有唯一整机状态，启动调用 `status_init()`，每次上电只初始化一次。
重复调用返回 `-EALREADY`，初始化失败后也不重新初始化硬件。业务阶段通过
`status_set_phase()` / `status_phase_get()` 访问，健康检查使用 `status_progress()`。
`status_application_fault_get()` 读取一致的最近故障快照；任务详情使用
`status_fault_get()`。故障记录不跨复位保存，故障路径不写 Flash。

指示灯使用 GPIO，初始化失败立即返回终止故障。仅配置成功才允许后续引脚访问；
首次离开 `STATUS_STARTING` 时启动周期工作，运行态按错过周期奇偶翻转，故障强制常亮。
协议层通过 `status_update_degradation()` 提交离线数量和存储诊断，
`status_degradation_get()` 返回一致副本；远端离线和允许的存储降级不直接触发终止。

从应用目录构建调试固件：

```bash
./scripts/build.sh --extra-conf "$PWD/conf/debug.conf"
```

例如让某处运行期故障留驻，直接写：

```c
status_report_fault(STATUS_RUNTIME, err, "Sample publication failed", STATUS_ACTION_HOLD);
```

新增故障时，在调用点给出初始化／运行阶段、错误码、英文常驻描述和枚举动作，
按业务有效性决定能否继续，无需增加来源枚举或 Kconfig 动作配置。
验收使用 `bash tests/runtime/run.sh`，采样单场景使用 `tests/acquisition/run.py`；
测试以独立进程隔离每次启动，不通过清零内部状态模拟重启。完整入口为 `bash tests/run.sh`。

整机状态、外部看门狗与任务期限监控保留在 status.c 内，以分段注释分类。
LED 单独位于 status_led/status_led.c/.h，名为 status_led 的延时工作独立更新 GPIO；
status.c 的后台工作只检查外部喂狗错误。两者均使用系统工作队列，不新增工作线程。
进入运行时启动 LED 周期工作，故障时提交立即更新请求；初始化失败后不访问 LED 引脚。
