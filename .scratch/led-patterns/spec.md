# 指示灯阶段图案表

## 需求

2026-10-27：用户要求按整机状态选择不同的闪烁间隔或常亮，把显示策略集中成一张表，
替代 `status_led.c` 中按阶段散落的判断，并保持实现通用。

## 设计

- `status_phase_t` 增加 `STATUS_PHASE_COUNT`，仅用于内部定长显示表。
- `status_led.c` 中的 `phase_patterns` 是阶段到图案的唯一映射：

  | 阶段 | 图案 | 参数 |
  | --- | --- | --- |
  | `STATUS_UNCONFIGURED` | 常灭 | - |
  | `STATUS_STARTING` | 慢闪 | 亮/灭各 500 ms |
  | `STATUS_RUN` | 快闪 | 亮/灭各 100 ms |
  | `STATUS_FAULT_LOCKED` | 常亮 | - |

- 显示引擎支持常灭、常亮和等时闪烁；新增阶段只需补表，不修改调度逻辑。
- `status_led_sync()` 在阶段切换时记录待显示阶段及切换时刻，工作队列仍是 GPIO 的唯一写入者；
  工作按切换时刻计算当前电平和下一个边沿，不补放已错过的周期。
- `status_led_init()` 配置 GPIO 并应用当前图案；`status_set_phase(STATUS_RUN)` 和故障回调
  调用 `status_led_sync()`，不再分别调用 start/refresh。
- GPIO 写入失败时，启动阶段按 `WATCHDOG_ACTION_HOLD` 上报，其他阶段按
  `WATCHDOG_ACTION_RESET`/200 ms 上报；故障阶段本身不再递归上报。
