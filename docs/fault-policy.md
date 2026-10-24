# 本地故障策略与诊断

策略位于 `app/status/Kconfig`，来源与动作映射位于 `app/status/status.c`。更改 `.conf` 后重新构建生效，不提供运行期 Modbus/Shell 改写或持久保存。

`status.c` 私有持有唯一的整机状态，启动调用 `status_init()`，调用方不创建或传递状态对象。
每次上电只初始化一次；重复调用返回 `-EALREADY`，初始化失败后也不重新初始化硬件。
阶段通过 `status_set_phase()` / `status_phase_get()` 访问，故障通过 `status_report_fault()`
报告、`status_application_fault_get()` 读取副本。协议层通过 `status_update_degradation()`
提交离线数量和存储诊断，`status_degradation_get()` 返回一致快照；不暴露内部可写指针。
LED 首次调度由离开 `STATUS_STARTING` 的阶段转换决定，不另存 `running` 标志。

初始化不再取消旧 LED 工作；产品没有同进程重新启动状态的功能。
采样生命周期测试使用 `tests/acquisition/run.py` 为每个场景启动独立 native 进程，
已接入 `tests/run.sh`。该套件的 Twister 条目只构建，运行验收必须使用上述脚本，
不能靠清零状态结构模拟整机重启。

| 配置后缀（前缀 `CONFIG_APP_FAULT_`） | 来源 | 可选值 |
| --- | --- | --- |
| `INPUT_INIT_ACTION` / `IAP_INIT_ACTION` / `PROTOCOL_INIT_ACTION` | 必需启动服务 | 1 留驻、2 复位 |
| `SAMPLE_ACTION` / `PUBLISH_ACTION` | 本地采样、交付 | 1、2 |
| `PROTOCOL_ACTION` / `IMAGE_CONFIRM_ACTION` | 协议维护、试运行确认 | 1、2 |
| `TASK_TIMEOUT_ACTION` | 显式登记任务超时 | 1、2 |
| `LED_ACTION` | LED 输出 | 0 记录继续、1、2 |

默认均为 2。喂狗输出失败固定复位，不提供可选项。终止后 DP/CO 拒绝新业务，北向静默，IAP 拒绝新上传；保留 Shell、日志和 RAM 快照。普通留驻继续喂外狗，调试留驻不启动看门狗。首个终止动作锁存，仅喂狗输出失败可将留驻升级为复位；停止业务产生的后续取消错误不会改写原故障。已经开始的操作到安全边界后停止，不能清除锁存故障恢复业务；详细提交边界见 [ADR 0011](adr/0011-terminal-faults-and-runtime-budgets.md)。

从应用目录构建调试固件：

```bash
./scripts/build.sh --extra-conf "$PWD/conf/debug.conf"
```

`conf/debug.conf` 显式设置 `APP_DEBUG_SKIP_WATCHDOG=y`，保留 MCUboot 使 PC13 浮空的状态，关闭任务期限监控，并把其他复位动作转换为留驻。正常构建不传该配置，默认启用看门狗。

例如让 LED 故障记录后继续采样、发布故障留驻，在自定义配置中写：

```ini
CONFIG_APP_FAULT_LED_ACTION=0
CONFIG_APP_FAULT_PUBLISH_ACTION=1
```

新增来源时，在 `status_fault` 增加来源，在 Kconfig 声明允许动作与默认值，在 `policies` 显式映射；故障出口调用 `status_report_fault()`。先判定此故障后是否仍能安全提供有效业务结果，再决定是否允许继续；当前实现仅允许 LED 继续。若增加另一类可继续故障，同步更新该限制与测试。通过 `status_application_fault_get()` 读取一致 RAM 快照，包含首错与终止来源；任务详情通过 `status_fault_get()` 查询。记录不跨复位保存，故障路径不写 Flash。日志记录故障来源枚举、阶段、错误码和动作。

验收使用 `bash tests/runtime/run.sh`：真实主循环/协议装配覆盖复位、留驻、调试、LED 继续、部分初始化失败与降级。IAP 提交边界由 `tests/iap/run.sh` 的独立进程场景覆盖。完整回归入口为 `bash tests/run.sh`。
