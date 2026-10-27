# 按装配开关异步采样管线

Status: open
Blocked by: None
优先级：P1

## 目标

当前产品只用同步 `dp_set_sig_value()`，异步样本提交、样本消费者和完成通知不应
占用静态 RAM。通过配置开关保留重新接入采集时的完整路径。

## 证据

- `points.c` 无条件定义并初始化：
  - `NET_BUF_POOL_FIXED_DEFINE(sample_pool, 6+2, 552 B)`：4416 B noinit + 224 B 池头；
  - `dp_samples_chan` 消息缓冲：552 B bss；
  - `dp_processed_chan` 与通知池：约 80 B；
  - `dp_init_config()` 无条件申请 `sample_batch`：552 B 堆。
- `dp_samples_task()`、`dp_submit_sig_values()`、`dp_process_submit()` 当前未进入
  最终镜像（链接器回收），但上述静态池仍保留，因为初始化代码引用了通道和池。
- 产品没有 `dp_processed_chan` 观察者；`acquisition` 使用同步接口。
- 合计可回收约 5.7 KB RAM（4.6 KB noinit + 0.55 KB bss + 0.55 KB 堆）。

## 方案

1. 新增 `CONFIG_DP_ASYNC_SAMPLES`（默认 `y`，保持模块通用能力）。
2. 关闭时：
   - 不定义采样池、`dp_samples_chan`、`dp_processor`、`sample_batch`；
   - `dp_submit_sig_value(s)`、`dp_publish()`、`dp_wait_processed()` 返回 `-ENOTSUP`；
   - `dp_init_config()` 不设置 zbus 池、不申请 `sample_batch`。
3. 让 `CONFIG_COLLECT` 或 `CO_VPORT` 在重新接入时 `select DP_ASYNC_SAMPLES`，
   避免出现“采集已启用但样本入口被关掉”的静默错误。
4. `dp_processed_chan` 的发布是否保留单独决定：当前无观察者，可在
   `CONFIG_DP_PROCESSED_EVENTS` 关闭时短路 `zbus_chan_pub()`。

## 验收

- 产品构建 `.noinit` 减少约 4.6 KB，`.bss` 减少约 0.6 KB，`sample_batch` 不再申请。
- 关闭配置时，同步写点、`initialize_all`、本地读取和停止路径与现在一致。
- 打开配置时，`pipeline`、`dataprocess`、`reference_services` 的异步样本用例通过。
- 构建期检查：`COLLECT=y` 且 `DP_ASYNC_SAMPLES=n` 时直接失败或自动选择，不能运行期静默。
- 在 `docs/memory-management.md` 记录开关与容量关系。

## 风险

- `zbus` 通道是编译期对象，开关只能通过条件编译实现；需确认 `points.h` 的
  `ZBUS_CHAN_DECLARE(dp_processed_chan)` 在关闭时仍可被观察者使用，或一并加条件。
- 关闭后 `dp_healthy()` 的 `last_progress` 不再由样本更新，当前无调用者，但需在
  接口文档中说明。
