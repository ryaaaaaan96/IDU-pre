# DP 模块审查清单

日期：2026-10-27。优先级：P0 正确性/契约，P1 高收益且低风险，
P2 需要产品决策，P3 清理与性能。

| # | 优先级 | 问题 | 证据 | 预期收益 | 任务 |
| --- | --- | --- | --- | --- | --- |
| 1 | P0 | 停止不唤醒等待者，`K_FOREVER` 等待无法退出 | `dp_stop()` 只置原子位；`dp_wait_processed()` 只在完成时广播 | 修正停止契约，避免线程永久阻塞 | [01](issues/01-stop-wait-semantics.md) |
| 2 | P1 | 每笔同步事务申请约 3.7 KB 工作区 | `dp_process_execute()` 每次 `k_malloc` 24 项 `entries`/`group` | 去掉每周期约 46 KB 分配/释放抖动 | [02](issues/02-reuse-transaction-workspace.md) |
| 3 | P1 | 异步采样管线静态占约 5.7 KB，但当前无发布者 | 采样池 4416+224 B、通道 552 B、`sample_batch` 552 B | process-only 直接省约 5.7 KB | [03](issues/03-gate-async-sample-pipeline.md) |
| 4 | P2 | 容量按通用上限配置，未按 26 点收敛 | `DP_POINTS_MAX_POINTS=64`、`DP_PROCESS_TIMERS=8`、`DP_MAX_DEVICES=16` | 约 1 KB 静态 RAM；容量约束更明确 | [04](issues/04-shrink-capacities.md) |
| 5 | P2 | 参数引擎编入但不挂载 | `CONFIG_DP_PARAMS=y`、8192 B 堆预算、472 B bss | 明确边界；不在用时省约 8 KB 预算 | [05](issues/05-decide-params-engine-boundary.md) |
| 6 | P2 | 多套初始化入口，错误只返回 -1 | `dp_init/dp_init_config/dp_params_init/dp_process_init/dp_reference_init` | 降低装配出错概率，保留 errno 诊断 | [06](issues/06-consolidate-init-entry.md) |
| 7 | P2 | 参考表路径仍被产品强制开启 | `app/Kconfig` 默认 `DP_REFERENCE_TABLES=y`，产品走模型 | 约 1–2 KB Flash、96 B RAM | [07](issues/07-disable-reference-table-path.md) |
| 8 | P3 | 热路径线性查找与整值拷贝 | `dp_config_index()` 每点线性扫描；`dp_read()` 复制 32 B 值 + 56 B 状态 | 大点表时降低 CPU；小点表收益有限 | [08](issues/08-hot-path-lookup-copy.md) |
| 9 | P3 | 未使用 API/配置与文档数值过期 | `dp_healthy()` 产品无调用者；`DP_*_STACK_SIZE` 仅供停用线程 | 减少误导，文档与实测一致 | [09](issues/09-cleanup-unused-api-config-docs.md) |

## 建议实施顺序

1. 先做 01（停止契约）和 02（事务工作区），两者都不改变产品数据语义。
2. 再做 03（异步管线开关），这是当前最大的静态 RAM 单项收益。
3. 然后 04、07 做配置收敛；05 需要先决定参数区是否重新挂载。
4. 06 在 05 之后统一入口；08、09 最后做。

## 验收原则

- 每个任务独立提交，保留用户已有的 `docs/forward-loopback.md` 和
  `scripts/forward-loopback/` 改动。
- 宿主/native_sim 证据与实板证据分开记录；不以编译通过代替实板验证。
- 资源收益以最终 ELF 的 `.bss`/`.noinit`、`kheap__system_heap` 符号和
  `CONFIG_SYS_HEAP_RUNTIME_STATS` 实测为准。
- 涉及容量下调时，必须同时提供构建期或启动期的越界拒绝，避免点表增长后静默失败。

## 已确认不做的方向

- 不为省 RAM 破坏 `dp_*` 参考接口语义、NVS 键/格式或点值字节序。
- 不删除停用的通信源码来换尺寸；通过配置和装配边界收敛，保留重新接入路径。
- 不把参数区默认值改为 NVS 覆盖，保持“已有 NVS 优先于默认”的现有语义。
