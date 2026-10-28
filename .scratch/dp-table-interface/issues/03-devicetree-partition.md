Status: resolved
Blocked by: None

# 设备树直接绑定参数分区

删除 dp_register 和运行时分区配置。模块要求 dp-params 别名指向启用的固定分区，
缺失时编译失败。板级改用通用名称，原分区位置、大小及 NVS 格式不变。
验证核心、跨进程持久化、产品装配、缺少别名的编译拒绝和固件构建。

## 验证记录

2026-10-28：native_sim dp_core 1/1、process_runtime 1/1 通过；同一模拟
Flash 跨进程 main/verify 保存、读取、恢复默认通过。移除 dp-params 别名后，独立
构建按预期失败，并出现模块明确的 requires a dp-params devicetree alias 提示。

重复登记先检查设备 ID，避免被容量检查遮蔽，稳定返回 -EEXIST。
本轮未进行实板烧录或验证。

默认 MCUboot + IAPv3 固件构建、签名验证与导出通过；git diff --check 通过。
