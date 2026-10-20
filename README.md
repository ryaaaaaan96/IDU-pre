# IDU-pre

面向 GD32E505 / MV-IO 硬件的 Zephyr 4.4.2 应用，提供本地采样、通讯点表、
Modbus 采集与转发、NVS 参数持久化及可选 MCUboot 升级。
通用能力由 `dataprocess`、`collect`、`transmit` 和 `quicklz` 四个模块提供。
默认使用虚拟南向采集口，现有 USART2 / RS485 用于北向转发。

## 快速开始

本仓库位于 West 工作区的 `applications/IDU-pre/`；Zephyr、MCUboot 和 HAL
由工作区 `manifest/west.yml` 固定。配置 Python 环境和 Arm 工具链后，
从工作区根目录执行：

```bash
# 默认固件：旧 BOOT 布局
./applications/IDU-pre/build.sh

# MCUboot + 旧 IAPv3 / AES / SGZ 升级
./applications/IDU-pre/build.sh --mcuboot --iapv3

# 主机回归与 Zephyr native_sim 测试
bash applications/IDU-pre/tests/run.sh
```

首次恢复工作区或有意同步依赖时运行 `west update`。构建/测试脚本在未激活
虚拟环境时自动使用工作区 `.venv`。也可在本仓库中执行 `./build.sh`。
默认 HEX 导出为工作区 `build/hex/IDU-pre.hex`；MCUboot 产物使用独立目录。
配置选项、签名密钥、分区和首次安装步骤见[构建与升级](docs/build-and-upgrade.md)。

## 阅读文档

完整导航见 [docs/README.md](docs/README.md)。

| 要做的事 | 文档 |
| --- | --- |
| 了解模块职责、数据流与初始化顺序 | [架构设计](docs/collect-process-transmit.md) |
| 新增设备、测点或导入原表 | [点表维护指南](src/cfg/README.md) |
| 查询寄存器、质量和参数事务规则 | [点表与寄存器](docs/point-tables.md) |
| 使用通用模块/API | [模块接口](modules/README.md)、[DP 使用说明](modules/dataprocess/README.md) |
| 迁移参考平台调用 | [接口覆盖、差异与 Flash/RAM 分工](docs/reference-platform.md) |
| 核对 MV-IO 旧报文行为 | [MV-IO 兼容规则](docs/mvio-compatibility.md) |
| 构建、签名、使用 SMP 或 IAPv3 | [构建与升级](docs/build-and-upgrade.md) |
| 核对引脚、时钟与移植条件 | [硬件说明](docs/hardware.md)、[GD32 支持调研](docs/gd32-support.md) |
| 运行测试、查看已记录结果 | [验证说明](docs/testing.md) |

## 仓库布局

```text
IDU-pre/
├── boards/、soc/、dts/   # IDU 板定义、E505 接入及设备树
├── src/
│   ├── main.c           # 应用入口、采样周期与休眠
│   ├── app/             # 启动装配、本地样本映射、健康确认
│   │   ├── startup.c       # DP、采集总线、参数恢复与处理启动
│   │   ├── point_service.c # 本地样本到信号的映射
│   │   └── boot.c          # MCUboot 试运行健康确认
│   ├── hardware/        # GPIO、ADC、LED、外部看门狗
│   ├── cfg/             # devices/ 设备与型号；tables/ 三表及元数据
│   ├── process/         # 工程换算、业务回调、参数命令和旧参数导入
│   ├── communication/   # 端口、协议、路由与采集后端
│   └── iap/             # IAPv3、SGZ 及镜像校验
├── include/             # 应用接口
├── modules/             # dataprocess、collect、transmit、quicklz
├── conf/、sysbuild/     # 构建配置与 MCUboot 联合构建
├── tools/、tests/       # 导入、镜像工具及回归测试
├── docs/                # 按主题组织的说明与历史记录
└── build.sh             # 构建入口
```

运行和构建不依赖旧工程或预编译 `plateform.lib`。重新导入和可选互操作测试
才需要显式提供参考源码。产物在工作区 `build/`，开发签名密钥在
`build/keys/`，不进入仓库；清理前备份需要保留的密钥。

## 实现与验证范围

点表核心使用 Zephyr 的线程、互斥锁、zbus、消息队列、Modbus 和 NVS，
不增加 ARC 封装。同步 `dp_set_sig_value()` 的长度单位为字节；异步遥测使用
`dp_submit_sig_value(s)`，长度单位为 16 位 word，成功表示复制入队。

参考平台四个公开头文件的 89 个接口名称中，当前覆盖 79 个；
[接口说明](docs/reference-platform.md) 列出限制和其余 10 个入口的原因。
可选 AIDC 配置已迁入点表与部分业务，DSP 仍使用虚拟模型，UPP 完整协议未实现。

已记录的桌面验证包括主机回归、native_sim、固件构建和镜像检查。
Zephyr 版的实际启动、UART/DE 时序、Flash 掉电和升级回退仍需实板验证。
历史测试数量、容量和外部支持状态均以文档中的日期或提交为准。

## 原 README 入口

<a id="已运行的硬件基准与移植配置"></a>
<a id="引脚映射"></a>
<a id="已实现行为"></a>
[硬件基准、引脚与板级行为](docs/hardware.md) 已移至硬件文档。

<a id="基础寄存器接口"></a>
[基础寄存器接口](docs/mvio-compatibility.md#基础寄存器接口) 已移至 MV-IO 文档。

<a id="flash-与启动布局"></a>
<a id="构建与验证"></a>
<a id="mcuboot"></a>
<a id="iapv3"></a>
[Flash 布局](docs/build-and-upgrade.md#flash-与启动布局)、
[构建](docs/build-and-upgrade.md#默认固件构建)、[MCUboot](docs/build-and-upgrade.md#mcuboot)、
[IAPv3 / AES / SGZ](docs/build-and-upgrade.md#iapv3) 和[验证](docs/testing.md) 已按主题归档。
