# IDU-pre 文档导航

[项目入口](../README.md) · [领域术语](../GLOSSARY.md) · [通用模块](../modules/README.md)

当前技术基线为 Zephyr v4.5.0、SDK 1.0.1。文档只描述当前实现、使用方法和验证范围。

## 设计与使用

| 内容 | 文档 |
| --- | --- |
| 通用模块设计报告、API 与示例 | [Word](module-design-report.docx)、[HTML](module-design-report.html) |
| 编译、签名、Flash 分区与 IAP 升级 | [构建与升级](build-and-upgrade.md) |
| 设备点表、参数持久化 | [DP 模块](../modules/func/dataprocess/README.md)、[产品点表](../app/protocol/README.md) |
| 通信架构、协议选择与数值转换 | [通信设计](transmit-design.md)、[transmit 契约](../modules/func/transmit/README.md)、[Modbus 适配](../modules/func/modbus/README.md) |
| 故障动作、任务监控与指示灯 | [产品故障策略](fault-policy.md)、[watchdog 模块](../modules/func/watchdog/README.md) |
| 硬件、时钟、引脚和采样 | [硬件说明](hardware.md)、[GD32E505 模块](../modules/platform/gd32e505/README.md) |
| 堆、线程栈、缓存与容量 | [内存管理](memory-management.md) |
| 测试入口、当前结果与实板待验 | [验证说明](testing.md) |
| Modbus 实板读写与 DP 测试报文 | [手动测试](modbus-test.md) |
| 编辑器与代码跳转 | [VS Code 与 clangd](editor.md) |
| WSL / Windows J-Link 烧录与调试 | [远程调试](wsl-jlink.md) |

## 专项调试资料

[串口回环脚本说明](forward-loopback.md) 为保留的用户调试资料。
当前产品尚未恢复南向 collect，执行前需核对脚本与固件的协议配置；该资料不作为当前架构依据。

## 维护约定

- 模块 API、生命周期和边界以各模块 README 为准；产品接线、装配和策略写入对应主题。
- HTML 是设计报告源文件，更新后运行 `python3 scripts/docs/export_module_report.py` 生成 Word。
- 测试结果标明代码版本和执行范围，主机、模拟器与实板证据分别记录。
- 设计讨论、临时任务和中间重构过程不作为正式文档保留；历史通过 Git 查询。
- 日志、构建产物和签名密钥留在本地工作区，不加入文档提交。
