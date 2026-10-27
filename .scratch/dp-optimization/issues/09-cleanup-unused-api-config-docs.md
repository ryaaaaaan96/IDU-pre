# 清理未使用 API、配置与过期文档

Status: open
Blocked by: 03, 05, 07
优先级：P3

## 目标

删除或明确标注当前产品不再使用的 DP 公开接口和 Kconfig，避免下一次装配继续
继承误导性的容量与预算。

## 证据

- `dp_healthy()` 在产品代码中无调用者；仅 `tests/acquisition/src/main.c` 保留
  一个测试桩定义。其 `last_progress` 只在样本路径更新，process-only 下不会推进。
- `CONFIG_DP_POINTS_STACK_SIZE`、`CONFIG_DP_PROCESS_STACK_SIZE` 只被停用的
  `app/protocol/tasks.c` 和测试夹具 `tests/fixtures/dp_tasks.h` 使用；模块自身
  不创建线程。
- `CONFIG_DP_PROCESS_QUEUE_DEPTH`、`CONFIG_DP_REFERENCE_MAX_TIMERS` 在当前产品
  没有运行时使用者。
- `prj.conf` 仍写“两个远端设备和本地测点共需 162 字节”，而当前本地模型是
  26 行 / 63 words；`docs/memory-management.md` 的 DP 行需要随本轮决策复核。
- `modules/dataprocess/README.md` 部分段落仍按完整通信装配描述，未标注当前
  process-only 的可选边界。

## 方案

1. 对 `dp_healthy()`：删除声明和实现，或标注 `@deprecated` 并说明替代方案；
   若保留，补一个真正需要活性判断的调用方。
2. 把线程栈和队列深度 Kconfig 的 help 改为“仅在应用创建对应线程时使用”，
   或移到停用通信配置的专属 Kconfig。
3. 依据 03/05/07 的最终配置，重写 `docs/memory-management.md` 的 DP 行和
   `modules/dataprocess/README.md` 的“当前产品装配”一节。
4. 用构建产物中的实际数值替换 `prj.conf` 的 162 字节注释和文档中的旧预算值，
   并注明取样日期与构建配置。

## 验收

- `grep -rn "dp_healthy"` 只剩声明或零结果（取决于决策）。
- 文档中的 DP 容量、堆预算、静态 RAM 与实际 `.config`/ELF 一致。
- `docs/README.md` 导航和 `modules/README.md` 的 DP 行同步更新。
- 文档修改不改变代码行为；若有删除接口，相关测试和调用方一并更新。

## 风险

- 删除公开接口可能影响未在本仓库启用的下游装配；优先标注而不是立即删除。
- 文档数值容易再次漂移；建议在 `docs/testing.md` 记录取值日期和构建配置。
