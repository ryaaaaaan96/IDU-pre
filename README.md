# IDU-pre

面向 GD32E505 / MV-IO 硬件的 Zephyr 4.4.2 应用，提供本地采样、通讯点表、
Modbus 采集与转发、NVS 参数持久化及可选 MCUboot 升级。
点表链路由 `dataprocess`、`collect`、`transmit` 提供，其他通用能力见[模块列表](modules/README.md)。
默认使用虚拟南向采集口，现有 USART2 / RS485 用于北向转发。
[`vport`](modules/vport/README.md) 已用于现有 RS485 转发和虚拟采集收发；
`transmit` 内的[通信适配](docs/communication-ports.md)保留 Zephyr 的 RTU/RAW 实现，
将完整报文接入逻辑端口。绑定集中在 `app/cfg/ports.c`。

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
| 新增设备、测点或导入原表 | [点表维护指南](app/cfg/README.md) |
| 查询寄存器、质量和参数事务规则 | [点表与寄存器](docs/point-tables.md) |
| 使用通用模块/API | [模块接口](modules/README.md)、[DP 使用说明](modules/dataprocess/README.md) |
| 迁移参考平台调用 | [接口覆盖、差异与 Flash/RAM 分工](docs/reference-platform.md) |
| 核对 MV-IO 旧报文行为 | [MV-IO 兼容规则](docs/mvio-compatibility.md) |
| 构建、签名、使用 SMP 或 IAPv3 | [构建与升级](docs/build-and-upgrade.md) |
| 核对引脚、时钟与移植条件 | [硬件说明](docs/hardware.md)、[GD32 支持调研](docs/gd32-support.md) |
| 运行测试、查看已记录结果 | [验证说明](docs/testing.md) |

## 应用架构与模块边界

应用入口按三层组织，硬件访问和业务配置通过服务接口连接：

```text
app/main.c                            启动入口、采样周期与休眠调度
  └─ acquisition/acquisition.c        采集、发布及通信的执行流程
       ├─ modules/gpio                GPIO 输入及 IDU 原始采样
       ├─ status/status.c             业务阶段、故障记录、LED 策略和健康进度
       ├─ status/image_confirm.c      MCUboot 试运行健康确认
       ├─ process/points.c            点表初始化、采样映射及公共回调
       └─ modules/transmit/src/server.c   转发端口启动、重配置与恢复
```

`app/status/idu_status.h` 定义业务状态：未配置、启动、等待采样、
读取输入、发布点值、维护通信、健康检查及故障锁定。`main.c` 调用
`idu_acquisition_start()` 和 `idu_acquisition_cycle()`；故障时记录阶段和错误码，
停止喂狗并拒绝继续采集。`app/process/idu_process.h` 提供应用级设备服务。
芯片专用的 JTAG/SWD 配置归属 `boards/idu/idu_pre/board.c`。点表配置、处理、参数事务和协议兼容
继续分别位于 `app/cfg/`、`modules/` 下的三类通用模块和 `modules/transmit/src/mvio.c`。

新增功能时，硬件操作放入对应板级模块，设备数据与存储策略放入设备服务，
运行顺序由 `acquisition` 编排，状态、故障锁定和 LED 指示策略归 `status`；
周期和休眠仍由 `main.c` 管理。
`app/CMakeLists.txt` 汇总功能目录，各目录的 CMakeLists 负责本组源码及可选配置；
应用头文件与所属功能源码同目录，通用模块继续独立放在 `modules/`。
`tests/acquisition/` 覆盖业务阶段、启动失败、无效采样发布、处理超时、通信及升级确认故障；
`tests/adc/`、`tests/io/` 覆盖统一采样、通用输入和看门狗，均已接入 `tests/run.sh`。

引脚、极性、ADC 通道顺序、DI/拨码位序、地址偏移和喂狗时序由板级 DTS 配置，
平均次数由 `CONFIG_IDU_ADC_AVERAGE_COUNT` 配置。业务层统一通过
`idu_inputs_sample()` 获取快照；输入模块不认识点号，也不自动写 DP。
采样字段到测点的映射由 `process/points.c` 完成，点表定义仍在 `app/cfg/`。

LED 操作直接放在 `app/status/status.c`，通过 Zephyr LED API 控制：
启动和故障时常亮，每完成一次正常采样翻转一次；输出失败阻止喂狗与镜像确认。
后续状态闪烁也在 `status` 中实现，有多个产品复用需求时再抽取模块。

## 仓库布局

```text
IDU-pre/
├── boards/、dts/        # IDU 板定义、早期板级初始化及设备树
├── app/
│   ├── main.c          # 应用入口、采样周期与休眠
│   ├── acquisition/     # 采集、发布及通信执行流程
│   ├── status/          # 业务状态、LED 策略、健康进度及镜像确认
│   ├── cfg/             # 点表及端口/协议配置；inc/ 配置契约；tables/ 三表
│   └── process/         # 点表初始化、采样映射、工程换算及业务参数回调
├── modules/             # analog_input、gpio、watchdog 及点表/升级/SoC 模块
├── bootloader/          # MCUboot 配置、分区和联合构建
├── sysbuild/            # Zephyr 约定的薄入口
├── conf/                # 应用可选配置
├── tools/、tests/       # 导入、镜像工具及回归测试
├── docs/                # 按主题组织的说明与历史记录
└── build.sh             # 构建入口
```

运行和构建不依赖旧工程或预编译 `plateform.lib`。重新导入和可选互操作测试
才需要显式提供参考源码。产物在工作区 `build/`，开发签名密钥在
`build/keys/`，不进入仓库；清理前备份需要保留的密钥。

## 头文件与启动程序边界

应用以 `app/` 为头文件搜索根。采集、状态、处理头
分别与实现放在 `app/acquisition/`、`app/status/`、`app/process/`；配置契约在 `app/cfg/inc/`；
跨功能引用使用 `"status/idu_status.h"` 等路径；采样使用 `<gpio/idu_inputs.h>`；IAP 使用 `<iap/iap.h>`、`<iap/sgz.h>`。
通用模块继续保留各自的公开 `include/`，由 Zephyr 模块机制导出。

`app/` 根目录只保留 `main.c`、`CMakeLists.txt` 和 `Kconfig`。
各功能接口按下表引用，
调用通用模块时直接包含对应模块头，不通过应用汇总头间接引入。

| 接口 | 职责 |
| --- | --- |
| `acquisition/idu_acquisition.h` | 启动和周期执行 |
| `status/idu_status.h` | 业务阶段、故障状态、LED 策略及健康进度 |
| `<gpio/idu_inputs.h>` | 模块提供的 IDU 统一原始采样接口及快照 |
| `status/idu_image_confirm.h` | MCUboot 试运行健康确认 |
| `process/engineering.h` | 仅依赖标准 C 的工程量换算，可用于主机测试 |
| `process/idu_process.h` | 点表初始化、采样发布、健康检查及测点业务回调 |
| `cfg/inc/ports.h` | 产品常驻端口、协议和仿真配置声明 |
| `<transmit/server.h>` | 每端口启动、配置更新及健康维护 |
| `<transmit/registers.h>`、`<transmit/mvio.h>` | 参数寄存器服务及 MV-IO 协议 |
| `<collect/port.h>`、`<collect/simulator.h>` | 采集端口初始化及可选模拟设备 |

应用不再设置 `communication/`。`cfg/` 以 const 表提供接口名、路由、协议范围、
命令及模拟寄存器初值；`collect` 管理采集端口、轮询和仿真，`transmit` 管理
转发状态机、协议解析与命令分发，`vport` 管理链路和路由，`iap` 管理升级。
AIDC 参数编码转换属于业务，保留在 `process/serial_settings.c`；各模块不引用
IDU 配置头文件。虚拟模型后端只提供模型数据，不实现真实 UPP 链路。

[`bootloader/`](bootloader/README.md) 保存本板 MCUboot 配置、分区和联合构建设置。
MCUboot 源码仍使用 West 工作区中的独立仓库；`app/status/image_confirm.c` 负责应用健康确认。
[`modules/iap/`](modules/iap/README.md) 负责升级接收、解包和验签，分区／内存取自
设备树，密钥来源、镜像上限和保存策略由 `conf/iapv3.conf` 等 Kconfig 配置提供，
产品升级开关归 `app/Kconfig`，运行层和通信层直接调用 `iap_*`，不再保留
`app/iap/` 兼容目录。根 `sysbuild/` 仅保留 Zephyr 约定入口。

[`modules/gd32e505/`](modules/gd32e505/README.md) 通过 Zephyr 的 `soc_root` / `dts_root` 提供
E505 登记、设备树时钟配置和标准 ADC 驱动；芯片启动由主应用和 MCUboot 共用，
ADC 按配置启用。根目录不再单独设置 `soc/`。
IDU 的引脚、外部看门狗和分区属于板级及产品配置，分别留在 `boards/` 和 `bootloader/`。

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

## 点表暂缓的扩展

应用三表使用参考结构 `DM_SIG_CFG_S`、`MODBUS_RTU_TABLE_S` / UPP 模型表、
`MB_TABLE_CELL_T`，由 `DEV_TABLE_S` 关联。信号使用宏或枚举，业务回调直接绑定；
默认值、范围和恢复策略合并在 `app/cfg/tables/process.c` 的 const `parameters[]`。
预留 `idu_point_parameters_get()` 按国家查询接口，当前 DP 使用通用参数；
具体国家数据与选择策略尚未接入，见[配置说明](app/cfg/README.md#国家参数预留接口)。
不再设置 `point_metadata` 中间层。

按当前范围，以下扩展暂不启用，后续有需求再单独设计配置：

- **质量码寄存器映射**：10009、10014–10023 暂无映射；内部通信错误和质量状态仍保留。
- **采样过期时间**：不因样本年龄自动置 STALE；通信请求自身的超时和失败处理仍保留。
- **旧参数与 NVS 迁移**：不从旧 MV-IO 分区导入地址，不注册 schema 增量或长度迁移。
  新空分区使用默认值并正常保存、重启加载；已存在的不匹配 schema 拒写并报告错误，
  不自动擦除。升级已有设备前需另行决定迁移方案。保存地址默认 1，实际通信地址仍取拨码。

设备表、三张点表及默认数组均为 const 并保留在 Flash，运行值、队列和参数缓存留在 RAM。
详细维护方法见[点表配置说明](app/cfg/README.md)。

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
