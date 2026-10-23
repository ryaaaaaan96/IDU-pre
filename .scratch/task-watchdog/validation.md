# 显式任务看门狗验收

状态：三个任务已按顺序完成；桌面回归、固件构建及两轴审查通过，实板验证未执行。

## 范围与提交

实施起点：`710125bc40a47acebddcf7c80ac31e2993b09300`。

| 任务 | 实施提交 | 结果 |
| --- | --- | --- |
| 01：零登记持续喂狗 | `3a2e955` | 解除 GPIO 主循环租约和应用默认 DP 活性约束；明确业务故障仍可停喂。 |
| 02：登记与超时 | `463aab1` | status 公开接口驱动真实 Zephyr Task Watchdog，超时先停喂再冷复位。 |
| 03：休眠与诊断 | `395cdaf` | 显式永久休眠、重新登记、通道复用和一致 RAM 快照。 |

仅修改应用仓库；未修改上游依赖，未推送。现有主采样、DP、CO、TR、IAP 未登记。
当前 `.conf` 的两个通道仅是手动容量，任务时限由调用者传入。

## 测试证据

- 任务 01：先用公开边界复现旧期限造成停喂、DP 隐式检查造成采集失败，再修改实现；io 8 项、acquisition 17 项通过。
- 任务 02：登记与独立心跳先红后绿；超时回调未实现时，缺失心跳场景失败，加入锁存、停喂和复位后通过。
- 任务 03：永久休眠接口先红后绿；新增恢复失败、复用通道、恢复后超时、同伴超时和并发快照场景。
- 新套件共 10 个独立 native 进程场景。保留真实内核定时、Task Watchdog 和 GPIO 看门狗模块，只替换 GPIO 硬件和不返回的系统复位边界。
- 三个超时场景在复位入口检查首个 RAM 故障、实际后端停喂错误及不可撤销状态，然后退出进程；不在 ISR 休眠，也不从复位函数返回。
- 负向对照：暂时去掉超时路径的物理 `watchdog_stop()` 后，测试输出 `WATCHDOG_RESET_FAIL` 并失败；恢复后 10 个场景全部通过。此临时修改未提交。

完整回归从工作区根目录执行：

```bash
ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
```

结果：

- 完整测试脚本退出码 0；386 项普通 Ztest 执行通过，71 次普通 native 运行成功，另有 3 个不返回复位验收场景通过。
- Python 三组共 23 项通过；主机工程换算测试在 ASan/UBSan 下通过（本环境关闭 LeakSanitizer）。
- 日志未出现编译 `warning:`、`FAILED` 或 `ERROR`；参数区不兼容等用例的预期运行期诊断保留。
- 当前 IDU 的 MCUboot + IAPv3 构建、签名和导出通过；验证报告 `signature_verified=true`，`hardware_verified=false`。
- 应用 Flash 129900 B、RAM 104252 B；MCUboot Flash 36568 B、RAM 18096 B。

生成日志保存在工作区 `build/IDU-pre/task-watchdog/`；签名产物和报告在 `build/hex/IDU-pre-mcuboot-iapv3/`，均不提交。

## 审查及限制

[Standards 与 Spec 两轴审查](review.md)均无代码发现，实施状态文档已同步。
运行期故障诊断只保存在普通 RAM，重启清空，故障路径不写 Flash。

尚未执行实板 GPIO 波形、停喂后外狗复位、软件冷复位及 MCUboot PC13 交接验证。
当前 GPIO 封装不是原生 Zephyr watchdog device，不宣称具备 Task Watchdog 原生硬件 fallback 的全部能力。
现有业务任务的实际登记和心跳检查点留待后续单独确定。
