# 精简 status 单例与初始化生命周期

Status: resolved

## 用户决定与实现

2026-10-24：用户要求删除初始化里为旧工作清理而设置的同步取消操作，随后明确采用
无需传入状态对象的单例，并要求精简 status 设计。“报文”是“设计”的输入错误，
本次不修改通信报文。

- `status.c` 私有静态实例保存阶段、故障、降级诊断和镜像确认信息；移除公开的可写状态结构。
- `status_init()` 在短临界区内认领首次初始化，重复调用返回 `-EALREADY`。
  删除 `k_work_sync`、`k_work_cancel_delayable_sync()` 和独立的 `running` 标志。
- main、采样、协议与看门狗测试使用无实例参数的状态接口。协议只提交诊断值，
  测试通过阶段和快照检查行为，不直接清零产品内部状态。
- 删除采样故障的转调包装和 main 的重复故障日志，由 status 保留首错并输出故障信息。
  `startup()` 收到 status 初始化错误后直接返回，重复启动不会被误报成新的输入故障。
- 采样测试的两个循环故障矩阵拆成独立 Ztest 场景，每个场景以新进程运行。
  不增加产品 reset/deinit 或测试专用重置接口；故障恢复仍以整机复位为界。
- 原有故障处理策略、LED 周期与迟到跳过行为、任务登记、留驻喂狗、存储降级均保留。

修改前唯一已有改动位于 `app/main.c`：用户删除了 startup 中重复的阶段检查。
本轮迁移该函数为无参入口，保留删除重复检查的意图；相关改动与本轮调用链一起提交。
修改前补丁另存于工作区 `build/IDU-pre/status-singleton/main-before.patch`。

## 验证

均使用当前工作区源码；native 运行设置 `ASAN_OPTIONS=detect_leaks=0`，不宣称完成泄漏验证。

| 验证 | 结果 |
| --- | --- |
| acquisition 构建及 `tests/acquisition/run.py` | 34 个独立进程场景通过 |
| task_watchdog 构建及既有 run.py | 12 个独立进程场景通过 |
| `tests/runtime/run.sh` | 20 个真实主循环／协议装配场景通过 |
| `CCACHE_DISABLE=1 ./scripts/build.sh` | Arm MCUboot + IAPv3 构建、验签、导出通过 |
| 本次 diff 空白检查 | 通过 |

采样测试增加验证：初始化前不能推进健康检查或报告业务故障；正常运行和启动失败后
重复初始化都被拒绝，既有故障不被覆盖，硬件初始化调用不重复。
此前的 LED 时序、阻塞采样期间闪烁、故障常亮和远端／存储降级断言继续通过。

新的 runner 初版把 `-list` 非零退出码当作列举失败；本地 Ztest 会在列举后因未运行套件
返回非零，但已输出完整列表。已按现有任务看门狗 runner 的方式验证列举内容，
实际执行仍必须退出码为零且出现成功标记。34 个场景均实际执行通过。

日志位于工作区 `build/IDU-pre/status-singleton/` 的 `acquisition-{build,test}.log`、
`watchdog-{build,test}.log`、`runtime.log`、`firmware-build.log`。
未执行全仓回归，未下载或执行新的实板固件；本次不是 J-Link 失联问题的修复。
