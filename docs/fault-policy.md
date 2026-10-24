# 本地故障策略与诊断

故障上报显式提供阶段、错误码、常驻描述和最终动作：

```c
status_report_fault(STATUS_INIT, err, "Input initialization failed",
                    CONFIG_APP_FAULT_INPUT_INIT_ACTION);
status_report_fault(STATUS_RUNTIME, err, "Watchdog feed output failed", STATUS_RESET);
```

`STATUS_INIT` / `STATUS_RUNTIME` 表示故障发生于初始化或运行阶段，与采样、发布等细分
业务阶段独立。错误码表示失败原因，描述说明故障来源；同一个 `-EIO` 可来自不同硬件。
描述使用英文，必须是字符串常量或始终有效的常驻字符串，函数保存指针，不复制或动态格式化。
`status` 不比较描述字符串、不维护故障来源枚举或动作映射表，直接执行调用方的最终动作。

动作仍在 `app/status/Kconfig` 集中配置，调用方直接传入对应配置值；更改 `.conf` 后
重新构建生效，不提供运行期 Modbus/Shell 改写或持久保存。

| 配置后缀（前缀 `CONFIG_APP_FAULT_`） | 来源 | 可选值 |
| --- | --- | --- |
| `INPUT_INIT_ACTION` / `IAP_INIT_ACTION` / `PROTOCOL_INIT_ACTION` | 必需启动服务 | 1 留驻、2 复位 |
| `SAMPLE_ACTION` / `PUBLISH_ACTION` | 本地采样、交付 | 1、2 |
| `PROTOCOL_ACTION` / `IMAGE_CONFIRM_ACTION` | 协议维护、试运行确认 | 1、2 |
| `TASK_TIMEOUT_ACTION` | 显式登记任务超时 | 1、2 |
| `LED_ACTION` | 运行期指示灯输出 | 0 记录继续、1、2 |
| `LED_INIT_ACTION` | 指示灯初始化 | 1、2；默认沿用 LED_ACTION，若为 0 则默认复位 |

正常构建的普通动作默认复位；`APP_DEBUG_SKIP_WATCHDOG=y` 时，未显式配置的普通动作
默认留驻。显式设置复位仍会执行，上报函数不再按调试标志改写最终动作。
喂狗初始化及输出故障由调用方直接传入 `STATUS_RESET`，不提供可选动作。
初始化故障禁止继续；直接向接口提交 `STATUS_INIT` 加 `STATUS_CONTINUE` 返回 `-EINVAL`。

首个故障的描述、阶段、错误和时间保留到复位，不被后续故障覆盖。快照另存当前终止
描述和动作；重复的相同终止动作不覆盖它，任何显式复位请求均可将留驻升级为复位。
故障锁存后不能恢复业务；继续动作也不会清除已锁存状态。日志输出描述、阶段、错误码和动作。

终止时 DP/CO 拒绝新业务，北向静默，IAP 拒绝新上传；保留 Shell、日志和 RAM 快照。
普通留驻继续喂外狗，调试留驻不启动看门狗。已经开始的操作到安全边界后停止，
不能撤销已执行的南向命令或升级提交，详细边界见 [ADR 0011](adr/0011-terminal-faults-and-runtime-budgets.md)。

`status.c` 私有持有唯一整机状态，启动调用 `status_init()`，每次上电只初始化一次。
重复调用返回 `-EALREADY`，初始化失败后也不重新初始化硬件。业务阶段通过
`status_set_phase()` / `status_phase_get()` 访问，健康检查使用 `status_progress()`。
`status_application_fault_get()` 读取一致的首错和终止描述快照；任务详情使用
`status_fault_get()`。故障记录不跨复位保存，故障路径不写 Flash。

指示灯使用 GPIO，初始化失败立即返回终止故障。仅配置成功才允许后续引脚访问；
首次离开 `STATUS_STARTING` 时启动周期工作，运行态按错过周期奇偶翻转，故障强制常亮。
协议层通过 `status_update_degradation()` 提交离线数量和存储诊断，
`status_degradation_get()` 返回一致副本；远端离线和允许的存储降级不直接触发终止。

从应用目录构建调试固件：

```bash
./scripts/build.sh --extra-conf "$PWD/conf/debug.conf"
```

例如让运行期 LED 故障记录后继续、发布故障留驻，在自定义配置中写：

```ini
CONFIG_APP_FAULT_LED_ACTION=0
CONFIG_APP_FAULT_PUBLISH_ACTION=1
```

新增故障时，在调用点给出初始化／运行阶段、错误码、英文常驻描述和最终动作；需要
可配置时在 Kconfig 声明动作及允许范围，按业务有效性决定能否继续，无需增加来源枚举。
验收使用 `bash tests/runtime/run.sh`，采样单场景使用 `tests/acquisition/run.py`；
测试以独立进程隔离每次启动，不通过清零内部状态模拟重启。完整入口为 `bash tests/run.sh`。
