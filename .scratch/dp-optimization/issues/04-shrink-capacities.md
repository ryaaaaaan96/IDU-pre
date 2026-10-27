# DP 容量按实际点表收敛

Status: open
Blocked by: None
优先级：P2

## 目标

把 `states[]` 和容量类 Kconfig 从“通用最大值”收敛到当前产品实际使用量，
并在点表增长时给出明确失败，而不是静默越界。

## 证据

| 配置 | 当前值 | 实际使用 | 影响 |
| --- | --- | --- | --- |
| `DP_POINTS_MAX_POINTS` | 64 | 26 | `states[64]` = 1536 B，实际约 624 B |
| `DP_POINTS_VALUE_WORDS` | 512 | 63 | 仅是上限，按实际字数申请，不占静态 RAM |
| `DP_PROCESS_TIMERS` | 8 | 0 | `timers[8]` = 256 B |
| `DP_MAX_DEVICES` | 16 | 1–2 | `reset_epoch[16]` = 64 B |
| `DP_POINTS_MAX_POLLS` | 16 | 0 | 无静态数组，仅校验 |
| `DP_POINT_WORDS` | 16 | 15 | 每个 `dp_value` 32 B，可省 2 B/值 |

## 方案

1. 优先把 `states[]` 改为按 `cfg->point_count` 在 `dp_init_config()` 中一次分配，
   与点值缓冲区同生命周期；失败路径与现有值缓冲区一致。这样产品配置不再决定
   静态 RAM。
2. 若保持静态数组，至少在 `prj.conf` 设 `DP_POINTS_MAX_POINTS=32`、
   `DP_PROCESS_TIMERS=1`、`DP_MAX_DEVICES=4`。
3. 在 `dp_validate()` 之外增加构建期或启动期检查：模型点数总和 ≤
   `DP_POINTS_MAX_POINTS`，否则启动失败并打印实际数量。
4. `DP_POINT_WORDS=15` 只在确认所有产品的最大测点宽度后设置；参考表和测试
   可能使用 16 words，需先核对。

## 验收

- 给出调整后的 `.bss`、RAM 总量和 `states` 实际分配字节数。
- `points`、`point_storage`、`dp_models`、`table_registry`、`process_runtime` 通过。
- 故意把模型点数加到超过上限时，初始化返回明确错误（`-EINVAL` 或 `-ENOSPC`），
  不产生越界写。
- 文档更新容量表和“改点表时需同步的配置”。

## 风险

- 动态 `states[]` 会把静态 RAM 变成堆预算，需同步调整 `DATAPROCESS` 预算，
  否则可能出现新的 `-ENOMEM`。
- 收缩 `DP_POINT_WORDS` 会影响参考表路径和测试夹具，不能只改产品配置。
