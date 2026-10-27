# IDU-pre

面向 GD32E505 / MV-IO 硬件的 Zephyr 4.4.2 最小应用：本地 DI/AI 采样、
点值存储、本机 NVS 参数和 MCUboot 升级。通信、采集调度和转发模块已移除；
DP 只保留单点同步读写和参数持久化。

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
| 本机参数 NVS 持久化、恢复默认、状态查询 | 可用 |
| 异步样本、业务线程、定时器 | 已移除 |
| 采集调度、Modbus 转发、vport | 已移除 |
| IAPv3 串口上传 | 模块保留，但当前无 TR/Modbus 路由，不可用 |

设置项（采样周期、标签、保存地址）保存在本机 `idu-settings` 分区；
参数区不可用时退回 RAM 默认值。当前 26 个本地测点、63 words。

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
  ├─ app/protocol/            本地模型和 dp_start()
  ├─ app/acquisition/         读取 DI/AI，同步写 DP
  ├─ app/process/points.c     采样周期、参数命令、参数状态
  └─ app/status/              整机状态、LED、故障
modules/dataprocess/          点值、质量、本机参数 NVS
modules/iap/                  IAPv3/SGZ 接收和解包
modules/modbus/               无分配 RTU 编解码和构建兼容
modules/watchdog/             任务监控与外部喂狗
modules/gd32e505/             SoC、时钟、ADC
```

启动顺序：`status_init()` → `acquisition_inputs_init()` → 可选 `iap_init()` →
`protocol_init()`（`dp_start()`）→ 镜像确认。主循环按 `points_sample_period_ms()`
采样并写点；终止故障后只留驻诊断，不恢复业务。

## 文档

完整导航见 [docs/README.md](docs/README.md)。

| 要做的事 | 文档 |
| --- | --- |
| 维护点表、参数和命令寄存器 | [协议点表](app/protocol/README.md) |
| 使用 DP 接口 | [DP 模块](modules/dataprocess/README.md) |
| 调整堆、点值或参数容量 | [内存管理](docs/memory-management.md) |
| 构建、签名、分区 | [构建与升级](docs/build-and-upgrade.md) |
| 核对硬件 | [硬件说明](docs/hardware.md) |
| 运行测试 | [验证说明](docs/testing.md) |
| 命名与采样边界 | [命名与采样边界](docs/naming.md) |
| 故障与看门狗 | [故障策略](docs/fault-policy.md)、[任务看门狗](docs/task-watchdog.md) |
