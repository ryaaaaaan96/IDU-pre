Status: resolved
Blocked by: None

# 点状态归属与查找简化

验收：无 slots、first、全局 states、点值 k_malloc；参数层无全局点下标。
验证多表/共享行定义的独立性、重复登记、状态缺失、整表校验失败不污染状态、
普通值/常量/命令/回调/持久参数读写及跨进程保存恢复。记录固件和静态 RAM。

用户修改：开始时 points.c 有 ISR 和容量检查的格式调整；ISR 格式保留为未提交改动，
容量分支随本轮删除目录上限一并删除。

## 验证结果（2026-10-28）

- native_sim dp_core 1/1：7 张表、共用只读行的独立状态、奇数字节数组、重复 ID、
  缺少状态数组、状态数组共用拒绝、重复信号表失败不污染状态；原单点长度、范围、
  常量、命令、读回调和参数读写通过。
- 同一模拟 Flash 跨进程 main/verify：两设备参数保存、重启读取、分别恢复默认通过。
- native_sim process_runtime 1/1：产品静态状态、DI、采样周期和参数状态通过。
- native_sim ADC 应用 6/6、驱动 8/8 通过。
- 默认 MCUboot + IAPv3 pristine 构建、签名验证及 HEX/BIN/SGZ 导出通过。
  首次 pristine 因无 SDK 自动发现失败；显式使用既有 gnuarmemb 工具链后通过。
- 固件 Flash 88,756 → 87,932 B；静态 RAM 40,760 → 40,856 B。
  local_states=1344 B，系统堆 16,988 → 16,476 B（含分配器开销）。
- git diff --check 通过（只检查本轮修改，保留用户 ISR 格式改动）。

未上板；不宣称 IAPv3 上传或实际掉电验证通过。参数存储编码未改动。
