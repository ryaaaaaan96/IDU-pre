# 热路径查找与拷贝优化

Status: open
Blocked by: 02
优先级：P3

## 目标

在点表规模增长时，避免每次读写都做全表线性查找和整值拷贝；当前 26 点规模下
收益有限，先记录方案，不优先实施。

## 证据

- `configuration.c:dp_config_index()` 对模型路径逐模型、逐点扫描 `sig_id`；
  每次 `dp_read()`、`dp_txn_get()`、`dp_reference_process()` 都可能调用。
- `dp_read()` 用 `memcpy(state->value.words, ..., point->width * 2)` 复制实际宽度，
  但 `struct dp_point_state` 本身是 56 B；`capture()` 和 `dp_txn_get()` 按整个
  `struct dp_value`（32 B）和状态赋值。
- `dp_set_sig_value()` 每次构造 1200 B 的 `dp_process_request`，即使只写 1 个
  2 字节点。
- 当前最大 15 words，512 B 上限下最多 64 点；收益要到数百点或高频采样才明显。

## 方案

1. 初始化时建立按 `(dev_id, sig_id)` 排序的索引表，二分查找；或使用开放寻址
   哈希。常驻 RAM 为每点 4–6 B，需计入堆/静态预算。
2. 为同步单点写入增加窄路径，避免构造完整 24 项请求；仍复用 02 的事务工作区。
3. 仅在 `point->width` 与 `sizeof(struct dp_value)` 差异大时做宽度感知拷贝；
   先测量再改，避免为小表增加分支。
4. 用 native_sim 的 cycle/µs 统计给出前后对比，不用主观判断。

## 验收

- 26 点规模下，单次本地写点和读取的 CPU 时间不劣化。
- 256 点合成点表下，查找耗时随点数增长接近对数而非线性。
- 新增索引的 RAM 计入 `kheap__system_heap` 或 `.bss`，并记录净变化。
- 不改变 `dp_*` 接口、键语义和点值字节序。

## 风险

- 索引表引入第二份键数据，必须保证与模型数组生命周期一致，且模型登记后不可变。
- 窄路径可能与异步路径产生两套事务校验逻辑；优先复用现有 `dp_process_execute()`
  的参数，而不是复制校验代码。
