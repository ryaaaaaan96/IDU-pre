Status: resolved
Blocked by: None

# 转发作为产品必需功能

删除 IDU_TRANSMIT 产品开关，无条件编译 port.c、forward.c，protocol_init 始终初始化端口。
通用 TRANSMIT 模块开关保留，产品 CMake 将其与 UART_INTERRUPT_DRIVEN 列为必需服务。
装配单测用测试替身检查端口初始化；真实串口转发仍由 protocol_port 套件验证。
删除配置符号后使用 pristine always 重建。

## 验收

产品装配、模拟串口测试和默认固件构建通过。

## 验证结果

- pristine always 重建：process_runtime 1/1、protocol_port 1/1 通过。
- 默认 MCUboot/IAPv3 全量构建、镜像验签和 SGZ 导出通过。
- 全量构建显式指定本机 gnuarmemb 15.3 工具链，避免依赖已清理缓存中的路径。
- 源码与测试配置无已删除配置符号；git diff --check 通过。
- 未进行实板验证。
