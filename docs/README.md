# IDU-pre 文档导航

[项目入口](../README.md) · [领域术语](../GLOSSARY.md)

## 当前文档

| 任务 | 文档 |
| --- | --- |
| 构建、分区与升级 | [构建与升级](build-and-upgrade.md) |
| 维护点表和本地参数 | [协议点表](../app/protocol/README.md)、[DP 模块](../modules/dataprocess/README.md) |
| 调整堆、点值或参数容量 | [内存管理](memory-management.md) |
| 测试与验证记录 | [验证说明](testing.md) |
| 核对硬件与引脚 | [硬件说明](hardware.md) |
| 命名与采样边界 | [命名与采样边界](naming.md) |
| 故障与看门狗 | [故障策略](fault-policy.md)、[任务看门狗](task-watchdog.md) |
| 配置代码跳转 | [VS Code 与 clangd](editor.md) |
| 在 WSL 调试实板 | [Windows J-Link 远程调试](wsl-jlink.md) |
| GD32 支持调研 | [GD32 官方支持调研](gd32-support.md) |

## 架构决策

参数生命周期和延后保存见 [ADR 0015](adr/0015-dp-device-storage.md)。
通信实现及设计见 [Transmit 与 DP 类型转换设计](transmit-design.md) 和
[ADR 0016](adr/0016-transmit-stream-and-dp-conversion.md)；transmit/vport、标准 Modbus 及 DP 转换已实现，硬件时序待实板验证。
当前有效决策见 [`adr/`](adr/)。旧通信相关 ADR 保留为历史记录，
新实现以 ADR 0016 为准，南向 collect 尚未恢复。

## 历史与证据

| 文档 | 用途 |
| --- | --- |
| [迁移与早期审计](migration-history.md) | 旧表、旧库和目录重构记录 |
| `legacy-point-inventory.csv`、`legacy-audit-evidence.json` | 旧平台点表与审计证据 |
| [任务记录](../.scratch/) | 按功能保存当时规格、讨论与验证 |

## 维护规则

当前行为写入对应主题；测试数量、容量和外部仓库结论须注明日期或提交。
构建产物、日志及签名私钥保存在工作区 `build/`，不加入文档提交。
主机、模拟器和实板证据分开记录。
