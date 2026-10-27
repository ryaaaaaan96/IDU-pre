# 复用事务工作区，去掉每笔写点的分配

Status: open
Blocked by: None
优先级：P1

## 目标

同步和异步事务共用一份在初始化阶段申请、生命周期贯穿运行期的工作区；
`dp_process_execute()` 不再每笔事务 `k_malloc()` 两个数组。

## 证据

- `process.c:dp_process_execute()` 每次申请
  `CONFIG_DP_PROCESS_MAX_OUTPUTS` 项的 `entries` 和 `group`。
  实测 `sizeof(struct entry)=112`、`sizeof(struct dp_point_update)=44`，
  即每次 2688 + 1056 = 3744 B。
- `data_process.c:dp_set_sig_value()` 每次调用还从堆申请
  `struct reference_buffers`（1408 B）。
- `acquisition_cycle()` 每周期 1 次 DI + 8 次 AI 写点，即每周期约 9 笔事务，
  约 46 KB 申请/释放。周期 500 ms 时约 92 KB/s，虽不并存但会造成碎片和
  `-ENOMEM` 故障路径。
- `dp_process_execute()` 已由 `process_lock` 串行化，工作区可以安全复用。

## 方案

1. 新增 `struct dp_process_workspace { struct entry entries[MAX]; struct dp_point_update group[MAX]; }`。
2. 在 `dp_process_init()` 中申请一次；同步调用前若尚未初始化，可延迟申请一次并缓存。
   失败只发生在首次使用，不进入每笔事务。
3. `dp_set_sig_value()`/`dp_fast_para_set()` 复用一份 `reference_buffers`，
   由 `reference_lock` 串行化；异步提交仍复制到独立作业。
4. `entries`/`group` 指针保持现有 `context_valid()` 的指针身份校验。
5. 评估把工作区改为静态 BSS 还是常驻堆：两者总 RAM 相同，但静态 BSS 可把
   `DP_WORK` 预算下调约 3.7 KB，且彻底消除该路径的分配失败。

## 验收

- `dp_process_execute()` 内部不再出现 `k_malloc()`/`k_free()`；用 `grep` 或
  代码审查确认。
- `dataprocess`、`points`、`pipeline`、`reference_dp`、`table_registry` 通过。
- 在 native_sim 上连续执行至少 10,000 笔单点写入，`sys_heap` 最大占用不随次数增长。
- 给出调整前后的 `kheap__system_heap` 和 `DP_WORK` 预算数值。
- 分配失败路径仍保留：首次工作区申请失败返回 `-ENOMEM`，之后可重试。

## 风险

- 若未来允许嵌套事务，复用工作区会冲突；当前 `owner` 已拒绝重入，需在接口文档中
  固化“单事务、不可嵌套”。
- 静态 BSS 方案会增加 `.bss`，需与堆预算的净收益一起记录，不能只报单侧数字。
