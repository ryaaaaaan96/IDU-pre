# 明确参数引擎的编译与挂载边界

Status: open
Blocked by: None
优先级：P2

## 目标

让“参数引擎已编译”和“参数区已挂载”成为两个显式状态，并让堆预算与实际挂载
一致；当前产品只挂载前者，却按后者预留 8 KB。

## 证据

- `CONFIG_DP_PARAMS=y` 是硬依赖：本地模型含 `FLAG_PERSISTENCE` 点，关闭后
  `dp_validate()` 会因 `DP_SOURCE_PARAMETER` 返回 `-ENOTSUP`。
- `app/protocol/process_runtime.c` 不调用 `dp_params_init()`；`dp_params_recover()`
  也不调用。`app/protocol/README.md` 明确写“使用 RAM 默认值，不挂载参数区”。
- 但 `CONFIG_HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS=8192` 仍计入共享堆；
  `struct parameter_buffers` 实测 7192 B，只有挂载时才会申请。
- `params.c` 仍有约 472 B 静态状态（`fs`、wire 缓冲、`status`、`defs` 等），
  `parameter_store.c` 约 1 KB 代码，均在 product 镜像中。
- 参数命令 `dp_params_restore/ flush / recover` 在未挂载时返回 `-EACCES`，但
  对应点仍出现在点表中。

## 方案（择一，先记录决策）

A. **重新挂载参数区**：在 `process_runtime.c` 中调用 `dp_params_init()` 和
   `dp_params_recover()`，恢复持久参数。需要恢复顺序、NVS 分区和故障降级测试。
B. **显式关闭挂载**：新增 `CONFIG_DP_PARAMS_MOUNT`（默认 `y`），产品设 `n`；
   关闭时 `HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS` 默认 0，`dp_params_init()` 返回
   `-ENOTSUP`，参数点读取直接走默认值，命令返回明确错误。
C. **保持现状**：只把 8192 B 预算改为“挂载时才计入”的 Kconfig 默认，并在文档
   中声明持久点当前只有 RAM 语义。

推荐先做 B 或 C 的预算修正；A 是产品行为变更，需单独确认。

## 验收

- 决策写入 `docs/adr/` 或 `.scratch/dp-optimization/spec.md` 的 Comments。
- 若关闭挂载：共享堆预算减少 8192 B，`kheap__system_heap` 符号同步变化；
  参数点读取仍返回默认值，`points_sample_period_ms()` 回退路径不变。
- 若重新挂载：`points` 参数套件、`parameter_records`、`points` 存储故障用例通过，
  并记录 NVS 分区擦除/迁移行为。
- 两种情况下 `dp_params_validate()` 的格式检查都不能被绕过。

## 风险

- 关闭挂载会让“持久参数”名不副实，需确认这是用户接受的过渡状态。
- `CONFIG_DP_PARAMS=n` 仍不可行，因为点表声明了持久点；不要用关闭编译来代替
  关闭挂载。
