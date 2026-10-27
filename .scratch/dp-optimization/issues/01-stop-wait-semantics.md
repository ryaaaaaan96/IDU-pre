# 停止时唤醒 DP 等待者

Status: open
Blocked by: None
优先级：P0

## 目标

`dp_stop()` 后，所有在 `dp_wait_processed()` 中等待的线程都应在有界时间内返回
`-ECANCELED`；不再依赖调用方必须传有限超时。

## 证据

- `points.c:dp_stop()` 只执行 `atomic_set(&stopped, 1)`。
- `dp_wait_processed()` 在条件变量上等待，只有 `complete_ticket()` 会
  `k_condvar_broadcast()`。
- 等待者若使用 `K_FOREVER`，停止后不会醒来；当前唯一调用方
  `dp_accept_samples(sync=true)` 使用 1 秒超时，所以问题被掩盖。
- `dp_business_task()` 不检查 `dp_stopped()` 退出，停止后仍会继续扫描已启用定时器。

## 方案

1. 用 ISR 安全的同步对象替代条件变量，或在 `dp_stop()` 中安排一次线程上下文广播。
   `dp_stop()` 的文档承诺可在 ISR 调用，不能直接跨上下文使用 `k_condvar_broadcast()`。
2. 可选方案：把完成通知改为 `k_event`/`k_sem`，`dp_stop()` 在 ISR 中也能 `give`；
   等待者用事件位 + 超时等待，并在醒来后检查 `dp_stopped()`。
3. `dp_business_task()` 在停止后应清空队列、给每个已接收作业一次完成通知，然后退出或
   永久休眠；不要在停止后继续重新武装定时器。
4. 同步更新 `points.h` 的等待接口说明和 `docs/` 中的停止语义。

## 验收

- 新增 native_sim 用例：线程以 `K_FOREVER` 等待一个未完成票号，另一线程调用
  `dp_stop()`，等待者在有限时间内返回 `-ECANCELED`。
- 停止后提交新样本、异步作业和等待均返回 `-ECANCELED`，不再分配内存。
- `pipeline`、`dataprocess`、`runtime` 现有用例通过。
- 在文档中说明停止不撤销已提交数据，只拒绝新业务并唤醒等待者。

## 风险

- 条件变量改为事件会触及 `dp_wait_processed()` 的锁语义，需在 Zephyr 4.4.2 上
  重新核对超时和 ISR 限制。
- 业务线程退出方式要与 `app/protocol/tasks.c` 的线程生命周期一致，避免留下
  无法重新创建的线程对象。
