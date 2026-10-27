Status: resolved
Blocked by: 01

# 产品装配收敛到 process_runtime.c

删除 device_binding.c/.h 和 protocol_bind_devices，将当前产品的设备/处理表选择与模型装配集中到 process_runtime.c。
当前局部 data_device_cfg 只有设备 ID 和处理表 ID；不恢复采集和转发配置。
停用通信配置直接使用已有目录装配；删除已移除产品转换接口的专用测试。

## 验证

- process_runtime native：1/1；table_registry：9/9；forwarding_devices：6 通过、1 迁移用例跳过。
- points 完整编译，筛选 test_product_port_registration_is_idempotent 通过，验证停用配置源码的引用完整。
- 固件构建、签名验证及 HEX/SGZ 导出通过。
- git diff --check 通过；未实板验证。

## Answer

当前装配只保留一个文件内的两字段配置和模型生成循环，不单设 binding 模块；停用通信没有被重新启用。
