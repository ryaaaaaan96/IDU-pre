# 模块导航

模块路径统一登记在 `module_paths.cmake`，产品通过 `modules.cmake` 加载。

| 层次 | 模块 |
| --- | --- |
| `func` | common、dataprocess、modbus、transmit、vport、watchdog、iap、quicklz |
| `dev` | fan、rs485、tpc5120 |
| `platform` | gd32e505 |

各模块接口与生命周期以模块内 README 和公开头文件为准。
IAP 内含其使用的 QuickLZ；独立 `func/quicklz` 作为备用模块保留，不默认登记。
CMSIS 与 GigaDevice HAL 使用工作区 west 管理的官方库。
产品按采集、协议、风机控制和 DP 回调分层装配，见 [产品设计](../docs/app-design.md)。
验证范围与尚未适配的历史测试见 [验证说明](../docs/testing.md)。
