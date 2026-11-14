# IDU-pre

面向 GD32E505 / MV-IO 硬件的 Zephyr 4.5.0 最小应用：本地 DI/AI 采样、
点值存储、本机 NVS 参数、Modbus 从机转发和 MCUboot 升级。
APP 管理通信线程，DP 负责显式类型读写和参数持久化。

## 快速开始

本仓库位于 West 工作区的 `applications/IDU-pre/`；Zephyr、MCUboot 和 HAL
由工作区 `manifest/west.yml` 固定。配置 Python 环境和 Arm 工具链后，
从工作区根目录执行：

```bash
# 默认 MCUboot + IAPv3 构建
./applications/IDU-pre/scripts/build.sh

# 主机回归与 Zephyr native_sim 测试
bash applications/IDU-pre/tests/run.sh
```

首次恢复工作区或有意同步依赖时运行 `west update`。构建/测试脚本在未激活
虚拟环境时自动使用工作区 `.venv`。产物位于 `build/hex/IDU-pre-mcuboot-iapv3/`。
配置选项、签名密钥、分区和首次安装步骤见[构建与升级](docs/build-and-upgrade.md)。

## 当前能力

| 能力 | 状态 |
| --- | --- |
| MCUboot 签名启动与镜像确认 | 可用 |
| 本地 DI/AI 采样和点值存储 | 可用 |
| 本机参数 NVS 延后保存、恢复默认、设备删除 | 可用 |
| 异步采样和业务调度 | 已移除；DP 仅保留参数保存线程 |
| Modbus 从机转发、vport | 已实现并通过模拟串口验证，实板待验 |
| 南向采集调度 | 未恢复 |
| IAPv3 串口上传 | 地址段路由已接入，模拟器完整升级通过，实板待验 |

设置项（采样周期、标签、保存地址）保存在 `dp-params` 总分区内的设备 NVS。
当前点表 24 行，DI/AI 等非持久点只存 RAM。参数更新后默认五秒延后保存，
主动重启前需停止修改并调用 `dp_flush()`；布局不匹配会清空 DP 参数区并重建，重新使用配置初值。
接口与存储布局见 [DP 模块](modules/func/dataprocess/README.md)。
保持寄存器 16 写 1 可同步保存；地址 0 为持久采样周期，17 为非持久对照值，
18 为操作命令（1 保存后重启、2 恢复默认、3 恢复出厂）。
重启对比报文见 [Modbus 手动测试](docs/modbus-test.md)。

## 串口调试命令

默认固件在 USART0（PA9 TX / PA10 RX，115200 8N1）启用 Zephyr Shell，
提示符为 `AIDC:~$ `。常用命令：

```text
help
clear
kernel reboot cold
kernel version
kernel uptime
kernel thread list
kernel thread stacks
kernel heap
device list
```

`kernel heap` 查看共享系统堆占用，`kernel thread stacks` 查看线程栈使用估计。
日志与 Shell 共用 USART0；`CONFIG_LOG_CMDS` 和 `CONFIG_LOG_RUNTIME_FILTERING`
已启用，可用 `log status`、`log enable inf app_protocol` 等命令调整。
详细规则见 [Zephyr Logging 文档](https://docs.zephyrproject.org/latest/services/logging/index.html)。

## 架构

```text
app/main.c                    启动、采样周期、故障留驻
  ├─ app/protocol/            DP 登记、协议装配与通信线程
  ├─ app/acquisition/         读取 DI/AI，同步写 DP
  ├─ app/process/points.c     读取采样周期
  └─ app/status/              整机状态、LED、故障
modules/func/dataprocess/          点值、回调、本机参数 NVS
modules/func/common/               共享数值类型、转换与放缩
modules/func/vport/                字节与事件输入、同步发送回调
modules/func/transmit/             协议识别、转发表与 DP 映射
modules/func/iap/                  IAPv3/SGZ 接收和解包
modules/func/modbus/               无分配 RTU 编解码和构建兼容
modules/func/watchdog/             任务监控与外部喂狗
modules/platform/gd32e505/             SoC、时钟、ADC
```

启动顺序：`status_init()` → `acquisition_inputs_init()` → 可选 `iap_init()` →
`protocol_init()`（DP 初始化、设备登记、转发装配）→ 镜像确认与启动等待。主循环按 `points_sample_period_ms()`
采样并写点；终止故障后只留驻诊断，不恢复业务。

## 文档

完整导航见 [docs/README.md](docs/README.md)。

| 要做的事 | 文档 |
| --- | --- |
| 维护点表和参数映射 | [协议点表](app/protocol/README.md) |
| 使用 DP 接口 | [DP 模块](modules/func/dataprocess/README.md) |
| 调整堆、点值或参数容量 | [内存管理](docs/memory-management.md) |
| 构建、签名、分区 | [构建与升级](docs/build-and-upgrade.md) |
| 核对硬件 | [硬件说明](docs/hardware.md) |
| 运行测试 | [验证说明](docs/testing.md) |
| 故障与看门狗 | [故障策略](docs/fault-policy.md)、[watchdog 模块](modules/func/watchdog/README.md) |
