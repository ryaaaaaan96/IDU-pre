# 任务看门狗移入应用层

用户明确要求任务看门狗逻辑直接在应用层实现，无需 `CONFIG_TASK_WDT` 配置。
这次替代上一轮自动选择该子系统的方案，实施基线为 `919aaf3`。

## 最终结构

- `app/status/task_watchdog/` 维护登记、心跳、永久休眠、首故障及期限检测，固定编入 status。
- 调用者提供常驻任务上下文和名称；只把活动项串成链表，无固定槽位、容量配置或堆分配。
- 一个单次 Zephyr 内核定时器跟踪最早到期期限，登记、心跳和休眠后重新选择；每次遍历开销为 O(活动任务数)。
- 没有活动项时不启动检查定时器，GPIO 外狗仍持续喂狗；普通休眠计入任务预算。
- 任一活动任务超时，先锁存 RAM 快照、停止物理喂狗，再请求软件冷复位。故障不可清除，重启后诊断清空。
- 产品和测试中的 `CONFIG_TASK_WDT*`、CMake 条件编译及底层 `task_wdt_*` 调用全部移除；产品通过 Kconfig 内部选择通用 `REBOOT` 能力。
- 公开 status 接口保持；不再因通道容量不足返回 `-ENOMEM`。现有业务任务仍未增加登记或心跳。

## 验证接缝

沿用已确认的公开 status API，真实执行应用层期限检测、内核定时和物理看门狗模块；仅替换 GPIO 硬件与不返回的系统复位边界。

先把原两通道限制的测试改为多任务登记：旧实现的第三次登记失败，得到行为红测；应用层实现后通过。
现在同时登记 19 个调用者上下文，验证活动项不依赖固定容量。
12 个全新 native 进程场景覆盖原生命周期行为，另增加最早任务心跳或休眠后不掩盖其他任务期限的复位验收。
既有 acquisition 服务编排测试继续使用其原有服务替身边界；完整定时行为由独立 task_watchdog 套件验证。

## 验证命令

从工作区根目录执行，日志位于 `build/IDU-pre/task-watchdog/app-timer-*`：

```bash
ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
```

结果：完整测试脚本退出码 0，386 项普通 Ztest 执行、71 次普通 native 运行、5 个不返回复位场景及 23 项 Python 测试通过。
主机工程换算测试在 ASan/UBSan 下通过（本环境关闭 LeakSanitizer）；日志无编译警告或失败标记。
12 个 task_watchdog 进程场景已包含在上述计数中。

当前 MCUboot + IAPv3 构建、签名和导出通过，产品及独立测试生成配置均为 `# CONFIG_TASK_WDT is not set`。
产物验证报告 `signature_verified=true`、`hardware_verified=false`。
应用 Flash 130068 B、RAM 104244 B；MCUboot Flash 36568 B、RAM 18096 B。

手工复核当前实现的锁顺序、最早期限重排、活动链表移除、休眠恢复、终止故障和 ISR 复位路径，未发现待修问题。
核对本地 Zephyr 内核定时器实现在调用应用回调前释放内部锁，应用回调可安全停止或重新启动定时器。
原两轴审查记录只适用于迁移前提交，不作为本次重写的独立审查结论。

实板 GPIO 波形、外狗与软件复位链及 MCUboot PC13 交接未验证。
