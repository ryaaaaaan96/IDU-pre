# 构建、分区与升级

[文档导航](README.md) · [验证说明](testing.md) · [硬件说明](hardware.md)

以下命令均从 West 工作区根目录执行。全部固件使用 MCUboot；默认启用 IAPv3，
无上传入口使用独立的构建/导出目录。旧 BOOT 构建和旧路径迁移代码已删除。

## 配置选择

| 需求 | 命令 |
| --- | --- |
| 默认 MCUboot + IAPv3 | `./applications/IDU-pre/scripts/build.sh` |
| MCUboot，无上传入口 | `./applications/IDU-pre/scripts/build.sh --no-upload` |
| MCUboot + IAPv3 | `./applications/IDU-pre/scripts/build.sh --mcuboot --iapv3` |

所有构建使用同一套 IDU 产品点表和业务。IAPv3 是唯一的镜像上传接收器，
`--no-upload` 只关闭接收器，不切换产品点表；不再保留 AIDC 产品分支。
当前产品不运行采集和转发，`modules/collect`、`modules/transmit`、`modules/vport`
及其应用装配已移除；重新接入需要新的 DP 适配层。

从包含旧产品分支的版本更新后，首次执行 `./applications/IDU-pre/scripts/build.sh --pristine always`
重新生成构建目录，清除已删除开关的配置缓存。

DP 最小核心的线程和容量配置集中在 `prj.conf`：

| 配置 | 当前值 | 说明 |
| --- | ---: | --- |
| `CONFIG_DATAPROCESS` | y | 启用点值和参数核心 |
| `CONFIG_DP_POINTS_MAX_POINTS` | 32 | 静态状态槽；当前 26 点 |
| `CONFIG_DP_PARAMS_MAX_COUNT` | 8 | 当前 3 个持久参数 |
| `CONFIG_DP_PARAMS_TRANSACTION_BYTES` | 2048 | 参数提交镜像上限 |
| `CONFIG_MAIN_STACK_SIZE` | 4096 | 主循环、本地采样和 DP 同步调用 |
| `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` | 2048 | 看门狗等系统工作 |

`app/CMakeLists.txt` 在配置阶段检查 `BOOTLOADER_MCUBOOT`、`DATAPROCESS`、
`GPIO` 和 `ADC`。模块仍可由其他应用或独立测试按需启用。

## Flash 与启动布局

产品镜像布局集中在 `bootloader/partitions.dtsi`，由板级 DTS 引用。
MCUboot 和应用分别通过 overlay 选择 boot 和 slot0；所有构建使用下节的同一布局。
旧参数主备区、旧升级配置及 BIN 暂存区的设备树声明已移除，不再参与运行。
AIDC 专用分区也已移除，0x014000–0x017fff 暂不分配；不会自动擦除其中旧内容。
IDU NVS、IAP 和 MCUboot 的有效存储偏移保持不变：

| 外部 Flash 内容 | 偏移 | 大小 |
| --- | --- | --- |
| IDU NVS 参数/事务 | 0x010000 | 16 KiB |
| IAP 日志（启用 IAP 时） | 0x018000 | 16 KiB |
| IAP SGZ 暂存（启用 IAP 时） | 0x201000 | 512 KiB |
| MCUboot slot1 | 0x300000 | 448 KiB |
| MCUboot scratch | 0x370000 | 8 KiB |

删除分区声明本身不擦除实物 Flash。参数后端读取到不兼容的存储格式时，
自动清空 `idu-settings` 并记录日志，以当前点表默认值重新初始化；不迁移旧整体快照。
新增或重排参数保留原逻辑键的值；I/O 故障报错停写。IAP 镜像和日志分区不参与参数清空。

## 默认固件构建

```bash
./applications/IDU-pre/scripts/build.sh
# 与显式 --mcuboot --iapv3 等价
```

应用构建位于 `build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/`，
导出位于 `build/hex/IDU-pre-mcuboot-iapv3/`。`tools/build_mcuboot.py` 调用
imgtool 验签，并检查 MCUboot 头、向量、镜像边界和 HEX/BIN 一致性，
生成 `IDU-pre-validation.json`。不再导出旧 `build/hex/IDU-pre.hex`。

工具链仍可通过 `ZEPHYR_TOOLCHAIN_VARIANT` / `GNUARMEMB_TOOLCHAIN_PATH` 等环境变量选择。
产品自定义配置使用可重复的 `--extra-conf /path/custom.conf` 和
`--extra-overlay /path/custom.overlay`；相对路径以工作区根为准。
清理构建缓存使用 `--pristine always`，不会删除 `build/keys/` 中的签名密钥。

测试命令、覆盖范围与已记录结果见[验证说明](testing.md)。

## 独立程序烧录

先启动 Windows J-Link GDB Server，在工作区根目录运行：

```bash
./applications/IDU-pre/scripts/flash.sh --host <Windows主机IPv4> --dry-run
./applications/IDU-pre/scripts/flash.sh --host <Windows主机IPv4>
```

该入口默认完整擦除内部 Flash、写入合并的 MCUboot＋签名应用 factory HEX、复位运行后退出。
它不构建、不进入交互调试、不清理外部 SPI NOR，拒绝仅含应用的 HEX。参数及首次迁移条件见 [Linux 独立烧录](wsl-jlink.md#linux-独立烧录入口)。

## MCUboot

MCUboot 负责启动校验、镜像安装和升级状态管理。本配置选择
ECDSA P-256 签名、`swap-scratch` 交换、试运行后确认；未确认的试运行镜像
在下次复位时回退。MCUboot 是独立启动程序，不是链接进主应用的一个任务。
官方说明：[Zephyr + MCUboot](https://docs.mcuboot.com/readme-zephyr.html)、
[升级与回退设计](https://docs.mcuboot.com/design.html)。

[IAPv3](#iapv3) 是唯一的应用级升级入口，接收封装签名 BIN 的 AES/SGZ 包。
包内必须是针对新地址构建并签名的 MCUboot 镜像。旧 `IAPV3_INFO` 不再作为新
启动程序的升级命令；参数只使用独立 NVS。

内部 Flash 和外部 NOR 的地址空间分别计算：

| 位置 | 地址或偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| 内部 Flash | `0x08000000` | 64 KiB | MCUboot |
| 内部 Flash | `0x08010000` | 448 KiB | slot0，包含 512 字节镜像头及尾部交换状态 |
| 外部 W25Q32 | `0x300000` | 448 KiB | slot1，接收新镜像/保存回退镜像 |
| 外部 W25Q32 | `0x370000` | 8 KiB | scratch，交换暂存 |

NVS 和 IAP 暂存地址保持固定，详见下节。
内部擦除页依据 GD32E50x 用户手册
为 8 KiB，写粒度设置为 4 字节；外部驱动使用 4 KiB 页布局。导出工具保守预留
完整 8 KiB 尾部，不接受超过 440 KiB 的签名升级文件。
MCUboot 在板级早期初始化中将 PC13 设为浮空输入；
应用 `status_init()` 恢复 GPIO 输出和喂狗。实物上仍需验证这一交接及擦写时序。

保留当前工具链设置，在工作区根目录执行：

```bash
# MCUboot + IAPv3，独立目录输出
./applications/IDU-pre/scripts/build.sh

# 后续版本：使用同一签名密钥，更新镜像版本
./applications/IDU-pre/scripts/build.sh --version 0.1.1+1
```

脚本首次生成本地开发密钥 `build/keys/IDU-pre-dev.pem`（权限 600），后续复用；
不是 MCUboot 仓库公开的示例密钥。清理整个 `build/` 会丢失此密钥，应另行备份；
正式设备使用 `--key /受控目录/签名密钥.pem`。固件和导出目录只包含公钥，
已经安装的 MCUboot 只能接受其公钥对应的签名。

`--no-upload` 的输出目录为 `build/hex/IDU-pre-mcuboot/`，默认 IAPv3 为
`build/hex/IDU-pre-mcuboot-iapv3/`。HEX 与升级 BIN 从同一次签名结果生成，
避免两次 ECDSA 签名造成内容差异；产物清单见 [IAPv3](#iapv3) 一节。

首次切换需要通过调试器安装新 BOOT 和新应用，**不能仅把新应用发给旧 BOOT**。
首次安装还应清空外部 `0x300000..0x371fff` 的新 slot1/scratch 区，保留参数和
NVS；普通后续升级由 MCUboot 管理交换区，不能擅自清空仍保存回退镜像的 slot1。
当前尚未配置实物烧录 runner，也没有执行擦除或烧录。Windows 主机连接 J-Link、WSL 通过远程 GDB 调试的操作见 [WSL/J-Link 方案](wsl-jlink.md)。

新应用完成输入、可选 IAP 和协议等必要初始化后，直接调用
boot_write_img_confirmed 确认一次，再进入运行状态；确认失败则启动留驻。
删除连续 10 秒、间隔 1.5 秒和 60 秒超时条件，参数存储降级与远端离线不阻止确认。
确认只证明必要初始化完成，不验证后续运行稳定性；确认后发生的运行故障不再触发试运行回退。
初始化失败时保持未确认，后续复位可由 MCUboot 回退。
升级/复位前应等待正在进行的参数操作和延后保存完成。

桌面验证包括原有回归测试、启动确认测试、固件交叉编译、官方 imgtool
验签以及签名破坏/错误密钥的拒绝检查。实际换版、掉电中断交换、回退、SPI NOR
时序和 watchdog 交接仍须上板验证。

```bash
source .venv/bin/activate
python applications/IDU-pre/tests/test_mcuboot_artifacts.py \
    build/hex/IDU-pre-mcuboot-iapv3
```

静态 RAM 的最新记录与历史容量对比见[验证说明](testing.md#静态-ram-记录)。

<a id="iapv3"></a>
## IAPv3 / AES / SGZ 兼容升级

接收、解包、验签和结果恢复由 [`modules/iap`](../modules/iap/README.md) 提供。
板级 DTS 的 `iap,staging`／`iap,journal` 选择分区，执行地址和
RAM 范围取自 `slot0_partition`／`zephyr,sram`；`conf/iapv3.conf` 配置镜像上限、
暂存容量、堆预算、AES 密钥及 DP 参数保存策略。公钥文件默认跟随 MCUboot
签名配置。分区属于板级布局，所有构建共用，不再使用单独的 overlay。
无需独立的 IDU IAP 适配 `.c`，启动顺序与初始化成功后的镜像确认仍由应用编排。
自动导出 SGZ 时读取本次构建的 `.config`；单独运行 `pack_sgz.py` 时也可用
`--config <应用 .config>` 保持密钥和上限一致。

适用当前 MV-IO 单 ARM 硬件。`modules/iap` 提供 `iap_route()`，但当前最小产品
没有编译 TR/Modbus 路由，`iap_init()` 初始化的接收器没有调用者；IAPv3 上传在
本构建中尚不可用。重新启用需要新增一个最小的 Modbus/RAW 路由适配层，并单独
验证总线窗口与上传时序。这是唯一的镜像上传入口；应用不再提供第二个写 slot1
的接收器。

```bash
./applications/IDU-pre/scripts/build.sh --mcuboot --iapv3
# 后续版本沿用同一签名密钥：
./applications/IDU-pre/scripts/build.sh --mcuboot --iapv3 --version 0.1.1+1
```

产物位于 `build/hex/IDU-pre-mcuboot-iapv3/`：

| 文件 | 用途 |
| --- | --- |
| `IDU-pre-factory.hex` | 首次通过 SWD 安装 MCUboot + 应用 |
| `IDU-pre-boot.hex` / `IDU-pre.hex` | 分开的启动程序 / 签名应用 HEX |
| `IDU-pre-update.bin` | 签名 MCUboot 镜像 |
| `IDU-pre-update.sgz` | 包含上述镜像的 SGZ；除前 16 字节外已做旧 AES 加密 |
| `IDU-pre-public.pem` / `IDU-pre-validation.json` | 公钥及产物校验记录 |

SGZ 生成器先用官方 imgtool 验签，然后封装单子文件、type=2、2048 字节块。
它输出 QuickLZ 的合法 stored 帧；接收端同时支持旧工具生成的实际压缩帧
（QuickLZ 1.5、level 3、无跨块历史）。没有把旧 FreeRTOS 分配器或压缩器链接进应用。
当前旧上位机软件未提供，尚不能判断它读取文件后是否再次执行 AES：默认 SGZ
适合直接分包发送；如果工具自己加密，使用 `pack_sgz.py --plain` 生成明文包，避免双重加密。

```bash
python applications/IDU-pre/tools/pack_sgz.py \
  --bin build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-update.bin \
  --key build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-public.pem \
  --output build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-update-plain.sgz --plain
```

协议保留的主要入口（地址均为线上原值，不减 40001）：

| 地址 | 行为 |
| --- | --- |
| 20049 / 20051 | 通信/协议版本 0x03000000；32 位数低字在前，每字大端 |
| 20055 / 20057 / 20059 | 首包延迟 3000 ms、分包间隔、状态超时 480000 ms |
| 20061 / 20062 | 下级设备数 0、属性 3 |
| 20063–20070 / 20071–20078 | 16 字节状态 / 原因；byte 15 为总状态，byte 0 为 ARM 状态 |
| 20083 | 0xAA 登录；0x55 退出；支持登录、保留字、包参数的合并写入 |
| 20088 / 20089 | 每包长度 / 总包数；长度 16–1024，16 字节对齐 |
| 20090–20094 | 包号从 1 开始、旧地址字段、本包长度、数据 |
| 20099–20103 | 首个缺包号、偏移、长度、缺包总数；读取总数为 0 时启动解包 |

FC03/04 读取、FC06/16 写入；FC16 按完整寄存器数判断长度，兼容旧 1 KiB
非标准长帧的 8 位 byte-count 字段。仅 IAP 地址段接受这种帧，普通 Modbus
边界由通用寄存器服务校验。IAP 广播 FC16 执行但静默，广播 FC04 保留旧应答，247
不进入 IAP。未知起点、截断帧、非法包号/长度等静默拒绝，整帧验证后才改变会话。
旧包地址字段仅保存/回读；写入位置与旧实现一致，由 `(包号 - 1) × 每包长度` 确定。
20093 仅决定最后一包的长度；非末包使用 20088 的每包长度，兼容该字段填 0 的发送端。

单包 ACK 表示已进入异步处理；只有解密、Flash 写入及回读比对成功后才清除
缺包标记。队列忙时静默，主机应按间隔发送并查询/补发缺包。同一包重发必须
内容一致；乱序包和跨扇区包均支持。Flash 按需擦除，解包线程独立于转发线程。

IAP 配置额外划分两个外部 NOR 区域：`0x18000–0x1BFFF` 为独立 NVS
升级日志，`0x201000–0x280FFF` 为 512 KiB SGZ 暂存。
slot1 位于 `0x300000`，不会从应用直接写当前镜像或 BOOT。
暂存容量与 `CONFIG_IAP_MAX_CONTAINER_SIZE` 成对维护：容器为签名镜像加上分包
开销，上限 0x6e000 的镜像即使按未压缩帧膨胀也在 512 KiB 以内。
日志使用固定 48 字节小端布局和自身 CRC；当前 IDU 的 DP 参数模块还启用全局
`NVS_DATA_CRC`。IAP 模块不切换该选项，迁移已有 NVS 数据时须保持此配置一致。

解包校验 SGZ CRC、子文件 CRC、输出长度与 CRC，然后校验 MCUboot 头、
本板向量、SHA256、签名密钥哈希和 ECDSA-P256。公钥从本次 MCUboot 构建的
同一个签名密钥生成；AES 只用于旧传输兼容。全部通过后，原子保存并暂时冻结
参数写入，提交升级日志，调用 `boot_request_upgrade(BOOT_UPGRADE_TEST)`，
等待 500 ms 后复位。原健康策略连续运行 10 s 才确认试运行镜像。

掉电中断收包不会续接未知数据，状态报失败，需要重新登录。升级后读取当前
镜像哈希，且等待镜像确认后才报告成功；回退到旧镜像报告失败。压缩差分包、
多个子文件、PLC/Eye/DSP 对下升级不支持；它们不会触发镜像切换。旧裸 BIN
及旧链接地址的固件也不能直接用于此配置。

验证入口：

```bash
bash applications/IDU-pre/tests/iap/run.sh
# 或运行包含原有点表、参数、MV-IO、启动策略的全部回归：
bash applications/IDU-pre/tests/run.sh
```

默认测试无需旧工程；依赖旧 QuickLZ 压缩器的两项互操作测试会明确显示为
跳过。若要运行这些额外测试，设置参考目录后执行：

```bash
IDU_LEGACY_IAP_DIR=/path/to/io-interface-board/code/APP/Project/Iapv3 \
    bash applications/IDU-pre/tests/iap/run.sh
```

桌面验证与实板待验证范围见[验证说明](testing.md)。
IAP 配置包含升级线程栈、缺包位图和解包缓冲区；容量记录见[验证说明](testing.md#静态-ram-记录)。
