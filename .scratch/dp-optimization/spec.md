# DP 模块优化审查

Status: reviewing
日期：2026-10-27

## 目标

在 process-only 装配下审查 `modules/dataprocess` 的接口、内存、并发和测试覆盖，
输出按优先级排序、可独立验收的任务清单。本轮只做审查和记录，不修改实现。

## 范围

- `modules/dataprocess/` 的公开头文件与全部源文件。
- 当前产品入口 `app/protocol/process_runtime.c`、`app/protocol/tables/process.c`。
- 直接覆盖 DP 的测试套件：`dataprocess`、`dp_models`、`points`、`point_storage`、
  `point_parameters`、`pipeline`、`parameter_records`、`process_runtime`、
  `reference_config`、`reference_dp`、`reference_services`、`runtime`、`table_registry`。

不在本轮范围：重新启用采集/转发、修改点表语义、修改 NVS 格式、实板时序。

## 事实基线

构建：`--pristine always` 默认 MCUboot + IAPv3，应用 Flash 106,100 B /
RAM 78,676 B；系统堆 45,404 B。

| 项 | 实测/配置 | 说明 |
| --- | --- | --- |
| 本地处理点 | 26 行 / 63 words / 最大 15 words | `tables/process.c` |
| `DP_POINTS_MAX_POINTS` | 64 | 仅 26 使用，`states[]` 静态 1536 B |
| `states[]` | 1536 B | 每点 24 B，实际约 624 B 需要 |
| 采样 zbus 池 | 4416 B noinit + 224 B 池头 | `DP_POINTS_QUEUE_DEPTH(6)+2` × 552 B |
| `dp_samples_chan` 消息 | 552 B bss | 异步样本通道 |
| `sample_batch` | 552 B 堆 | `dp_init_config()` 无条件申请 |
| DP `.bss`（最终链接） | 约 2.3 KB | 含 `states`、定时器、通道 |
| DP `.noinit` | 约 4.7 KB | 采样池与通知池 |
| DP 最终链接 `.text` | 17,982 B | 链接器已回收未引用函数 |
| DP 堆预算 | 20,736 B | `DP_WORK 12288 + DP_PARAMS 8192 + DATAPROCESS 256` |
| `struct dp_process_request` | 1200 B | 每笔事务/异步作业 |
| `struct entry[24]` + `group[24]` | 2688 + 1056 B | 每笔同步事务申请 |
| `struct reference_buffers` | 1408 B | 每次字节接口调用申请 |
| `struct parameter_buffers` | 7192 B | 参数引擎挂载时一次申请，当前未挂载 |
| 本地写入频率 | 每周期 1 次 DI + 8 次 AI | `acquisition_cycle()` |

`dp_samples_task()`、`dp_business_task()`、`dp_process_submit()`、
`dp_reference_collect()`、`dp_params_init()` 当前未进入最终镜像，但相关静态池仍因
`dp_init_config()` 的初始化而保留。

## 结论

DP 的功能回归覆盖面较好，主要问题不在算法，而在：

1. **资源与装配不一致**：按完整通信装配预留的池、容量和堆预算，当前 process-only
   路径大部分不使用。
2. **同步事务的堆开销偏高**：每次本地写点都申请约 3.7 KB 事务工作区，外加
   1.4 KB 字节接口缓冲。
3. **停止与等待契约不完整**：`dp_stop()` 不唤醒等待者，`K_FOREVER` 等待无法退出。
4. **初始化入口过多**：`dp_init`、`dp_init_config`、`dp_params_init`、
   `dp_process_init`、`dp_reference_init` 并存，产品只走其中一条。

具体任务见 [review.md](review.md) 和 `issues/`。

## Comments

2026-10-27：用户要求开始 DP 模块优化，并在方向选择时选择“先出审查清单”。
本轮完成源码、构建产物和现有测试覆盖的审查，只创建清单文件，未修改实现。
待用户确认优先级后，再按任务逐个实施。
