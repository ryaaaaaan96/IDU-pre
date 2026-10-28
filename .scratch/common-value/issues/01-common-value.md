Status: resolved
Blocked by: None

# 共享数值联合体

value_t/value_type_t 与 VALUE_TYPE_* 迁入 modules/common，容量配置改为
CONFIG_COMMON_VALUE_WORDS，默认 16。合并运行值和点表字面值，保留 array 初值语义。
DP 配置类型改为 dp_point_config_t，删除 dp_key_t，函数直接接收两个 ID。

## 验证

- native_sim：DP 核心、process_runtime、ADC（14 项）通过。
- IAP：8 项通过、2 项跳过；未覆盖跳过配置。
- 看门狗默认测试产生预期 WATCHDOG_RESET_PASS 复位标记，未宣称完整套件回归。
- 默认 MCUboot + IAPv3 固件构建及签名验证通过。
- 应用 Flash 90,332 B、静态 RAM 41,576 B；相比合并前 Flash 增加 1,992 B，RAM 不变。
- 参数 NVS 编码、哈希键与版本未更改；本轮未做实板验证。
- 原有 ISR 分行与头文件尾随空格改动留在工作区，不纳入提交。
