# 构建、分区与升级

[文档导航](README.md) · [验证说明](testing.md) · [硬件说明](hardware.md)

以下命令均从 West 工作区根目录执行。默认配置、SMP、IAPv3 和 AIDC 使用各自的
构建/导出目录；默认固件对应旧 BOOT 布局，MCUboot 对应新的启动分区。

## 配置选择

| 需求 | 命令 |
| --- | --- |
| 默认 MV-IO，旧 BOOT 布局 | `./applications/IDU-pre/build.sh` |
| MCUboot，保留普通转发 | `./applications/IDU-pre/build.sh --mcuboot` |
| MCUboot + SMP UART | `./applications/IDU-pre/build.sh --mcuboot --smp-uart` |
| MCUboot + IAPv3 | `./applications/IDU-pre/build.sh --mcuboot --iapv3` |
| MCUboot + AIDC 虚拟点表 | `./applications/IDU-pre/build.sh --mcuboot --aidc` |
| MCUboot + AIDC + SMP | `./applications/IDU-pre/build.sh --mcuboot --aidc --smp-uart` |

IAPv3 与 SMP、AIDC 互斥。真实南向串口配置见
[替换虚拟采集口](point-tables.md#第二路串口补上后)。

## Flash 与启动布局

内部 Flash：

| 区域 | 相对 Flash 起始偏移 | 大小 |
| --- | --- | --- |
| 旧启动程序 | 0x0000 | 16 KB |
| Zephyr 应用 | 0x4000 | 496 KB |

应用 HEX 链接到 `0x08004000`，BIN 对应的烧录地址也是
`0x08004000`。BIN 不自带地址。本工程不生成旧 BOOT + APP 合并固件。
旧链接脚本从 `0x08004000` 起仍声明 512 KB，这是越过芯片总容量的
布局；新分区从总计 512 KB 中扣除了启动区。

外部 4 MB SPI Flash 保留原偏移：

| 区域 | 偏移 | 大小 |
| --- | --- | --- |
| 参数主区 | 0x000000 | 4 KB |
| 参数备份 | 0x001000 | 4 KB |
| Zephyr NVS 参数/事务 | 0x010000 | 16 KB（4 × 4 KB） |
| 可选 AIDC NVS 参数/事务 | 0x014000 | 16 KB（4 × 4 KB） |
| 升级配置区 | 0x200000 | 1 MB，预留范围 |
| 升级镜像区 | 0x300000 | 1 MB |

旧参数和升级分区标为只读。参数模块只把独立 NVS 分区交给存储后端，
不写旧区，也不直接保存 C 结构体。SPI NOR 页布局按 4 KB 扇区配置。

参数保留与升级边界：

1. 旧参数导入已实现：首次空 NVS 或已知上一版参数格式升级时，只读
   校验主/备区的长度和 Modbus CRC16，解析 8 字节 `G_FLASH_T` 保存地址。
   新 NVS 成功提交后不再导入；已有其他参数和待恢复事务保留。
   CRC 正确但长度未知时拒写并保留现场。仍需实际 Flash 样本/掉电验证。
2. IAP 迁移已增加独立配置：保留 SGZ 接收偏移，解包后写入 MCUboot
   slot1，并校验 CRC、向量、SHA256、密钥哈希及 ECDSA 签名。
3. MCUboot 首次安装需要更新启动程序和应用；旧 BOOT 的 16 KiB 分区
   不适用于新的 64 KiB 启动程序。旧裸 BIN 需要重新构建和签名。

## 默认固件构建

从工作区根目录执行：

```bash
./applications/IDU-pre/build.sh
```

也可通过环境变量指定已安装的 GNU Arm 工具链，例如：

```bash
env ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
    GNUARMEMB_TOOLCHAIN_PATH=/path/to/arm-none-eabi-toolchain \
    ./applications/IDU-pre/build.sh
```

`CONFIG_KERNEL_BIN_NAME="IDU-pre"` 设置固件名称。
输出在 `build/IDU-pre/idu_pre/zephyr/`，包含 `IDU-pre.hex`、
`IDU-pre.bin` 和 `IDU-pre.elf`。构建脚本在成功后自动复制 HEX 到
`build/hex/IDU-pre.hex`，覆盖该项目上次导出的 HEX；不同板卡使用
同一项目名时，这里保留最后一次成功导出的版本。原构建目录保留全部
产物。构建失败时不会执行复制。当前未配置具体芯片后缀对应的烧录
runner。导出前自动执行 `tools/check_boot_image.py`，
检查应用边界、SP/Thumb 复位向量及 HEX/BIN 一致性，报告为构建目录的
`IDU-pre-boot-check.json`。这是静态检查，不能代替旧 BOOT 真机跳转验证。

测试命令、覆盖范围与已记录结果见[验证说明](testing.md)。

## MCUboot

MCUboot 可替代旧 BOOT 的启动校验、镜像安装和升级状态管理。本配置选择
ECDSA P-256 签名、`swap-scratch` 交换、试运行后确认；未确认的试运行镜像
在下次复位时回退。MCUboot 是独立启动程序，不是链接进主应用的一个任务。
官方说明：[Zephyr + MCUboot](https://docs.mcuboot.com/readme-zephyr.html)、
[升级与回退设计](https://docs.mcuboot.com/design.html)、
[Zephyr MCUmgr](https://docs.zephyrproject.org/latest/services/device_mgmt/mcumgr.html)。

标准 SMP 和[旧 IAPv3](#iapv3) 使用独立的应用配置，共用同一 MCUboot
签名和交换布局。SMP 接收签名 BIN；IAPv3 接收封装该 BIN 的 AES/SGZ 包。
包内必须是针对新地址构建并签名的 MCUboot 镜像。旧 `IAPV3_INFO` 不再作为新
启动程序的升级命令；旧参数导入和 NVS 仍按原流程工作。

内部 Flash 和外部 NOR 的地址空间分别计算：

| 位置 | 地址或偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| 内部 Flash | `0x08000000` | 64 KiB | MCUboot |
| 内部 Flash | `0x08010000` | 448 KiB | slot0，包含 512 字节镜像头及尾部交换状态 |
| 外部 W25Q32 | `0x300000` | 448 KiB | slot1，接收新镜像/保存回退镜像 |
| 外部 W25Q32 | `0x370000` | 8 KiB | scratch，交换暂存 |

外部参数区和原 NVS 保留。IAP 配置会使用原 SGZ 暂存区，详见下节。
内部擦除页依据 GD32E50x 用户手册
为 8 KiB，写粒度设置为 4 字节；外部驱动使用 4 KiB 页布局。导出工具保守预留
完整 8 KiB 尾部，不接受超过 440 KiB 的签名升级文件。
MCUboot 在 SoC 早期初始化中沿用旧 `wdog_close()`，将 PC13 设为浮空输入；
应用 `idu_health_init()` 恢复 GPIO 输出和喂狗。实物上仍需验证这一交接及擦写时序。

保留当前工具链设置，在工作区根目录执行：

```bash
# MCUboot + MV-IO 应用，独立目录输出
./applications/IDU-pre/build.sh --mcuboot

# 增加标准 SMP 升级入口：USART0，115200 8N1；USART2 继续转发 Modbus
./applications/IDU-pre/build.sh --mcuboot --smp-uart

# 可选 AIDC 业务表 + 标准升级入口
./applications/IDU-pre/build.sh --mcuboot --aidc --smp-uart

# 后续版本：使用同一签名密钥，更新镜像版本
./applications/IDU-pre/build.sh --mcuboot --smp-uart --version 0.1.1+1
```

脚本首次生成本地开发密钥 `build/keys/IDU-pre-dev.pem`（权限 600），后续复用；
不是 MCUboot 仓库公开的示例密钥。清理整个 `build/` 会丢失此密钥，应另行备份；
正式设备使用 `--key /受控目录/签名密钥.pem`。固件和导出目录只包含公钥，
已经安装的 MCUboot 只能接受其公钥对应的签名。

标准 SMP 配置输出在 `build/hex/IDU-pre-mcuboot-smp/`：

| 文件 | 用途 |
| --- | --- |
| `IDU-pre-factory.hex` | MCUboot 与签名应用合并，用于首次安装新布局 |
| `IDU-pre-boot.hex` | MCUboot 单独产物 |
| `IDU-pre.hex` | 签名应用 HEX，要求设备已有匹配的 MCUboot |
| `IDU-pre-update.bin` | 后续通过 SMP 上传的签名镜像 |
| `IDU-pre-public.pem` | 对应公钥 |
| `IDU-pre-validation.json` | 签名、向量、边界检查及产物摘要；明确记录未做硬件验证 |

不加 `--smp-uart` 的输出目录为 `build/hex/IDU-pre-mcuboot/`；AIDC + SMP 为
`build/hex/IDU-pre-mcuboot-aidc-smp/`。原 `build/hex/IDU-pre.hex` 始终属于旧 BOOT
布局。HEX 与升级 BIN 从同一次签名结果生成，避免两次 ECDSA 签名造成内容差异。

首次切换需要通过调试器安装新 BOOT 和新应用，**不能仅把新应用发给旧 BOOT**。
首次安装还应清空外部 `0x300000..0x371fff` 的新 slot1/scratch 区，保留参数和
NVS；普通后续升级由 MCUboot 管理交换区，不能擅自清空仍保存回退镜像的 slot1。
当前尚未配置实物烧录 runner，也没有执行擦除或烧录。

SMP 客户端执行“上传 `IDU-pre-update.bin` → 查询镜像 → 把新镜像设为 test →
复位”。新应用连续 10 秒完成采集、转发、健康检查，且参数存储就绪、没有待恢复
事务后自动确认；60 秒仍未满足条件则停止喂狗，由复位触发回退。此确认只证明
当前健康检查通过，不涵盖现场所有业务工况。客户端应采用 test 流程，避免人工
提前确认绕过应用自检。升级/复位前应等待正在进行的参数操作和延后保存完成。

桌面验证包括原有回归测试、试运行确认策略测试、固件交叉编译、官方 imgtool
验签以及签名破坏/错误密钥的拒绝检查。实际换版、掉电中断交换、回退、SPI NOR
时序和 watchdog 交接仍须上板验证。

```bash
source .venv/bin/activate
python applications/IDU-pre/tests/test_mcuboot_artifacts.py \
    build/hex/IDU-pre-mcuboot-smp
```

静态 RAM 的最新记录与历史容量对比见[验证说明](testing.md#静态-ram-记录)。

<a id="iapv3"></a>
## IAPv3 / AES / SGZ 兼容升级

接收、解包、验签和结果恢复由 [`modules/iap`](../modules/iap/README.md) 提供。
`boards/iapv3.overlay` 的 `iap,staging`／`iap,journal` 选择分区，执行地址和
RAM 范围取自 `slot0_partition`／`zephyr,sram`；`conf/iapv3.conf` 配置镜像上限、
AES 密钥及 DP 参数保存策略。公钥文件默认跟随 MCUboot 签名配置。
无需独立的 IDU IAP 适配 `.c`，启动顺序与试运行健康确认仍由应用编排。
自动导出 SGZ 时读取本次构建的 `.config`；单独运行 `pack_sgz.py` 时也可用
`--config <应用 .config>` 保持密钥和上限一致。

适用当前 MV-IO 单 ARM 硬件。现有 USART2/RS485 保持转发用途，升级地址段
在同一端口内路由；虚拟采集口继续工作。此配置与 `--aidc`、`--smp-uart`
互斥：AIDC 的升级入口/多设备升级尚未移植，也不允许两个镜像上传端同时写 slot1。

```bash
./applications/IDU-pre/build.sh --mcuboot --iapv3
# 后续版本沿用同一签名密钥：
./applications/IDU-pre/build.sh --mcuboot --iapv3 --version 0.1.1+1
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
边界仍由原路由校验。IAP 广播 FC16 执行但静默，广播 FC04 保留旧应答，247
不进入 IAP。未知起点、截断帧、非法包号/长度等静默拒绝，整帧验证后才改变会话。
旧包地址字段仅保存/回读；写入位置与旧实现一致，由 `(包号 - 1) × 每包长度` 确定。
20093 仅决定最后一包的长度；非末包使用 20088 的每包长度，兼容该字段填 0 的发送端。

单包 ACK 表示已进入异步处理；只有解密、Flash 写入及回读比对成功后才清除
缺包标记。队列忙时静默，主机应按间隔发送并查询/补发缺包。同一包重发必须
内容一致；乱序包和跨扇区包均支持。Flash 按需擦除，解包线程独立于转发线程。

IAP 配置额外划分两个外部 NOR 区域：`0x18000–0x1BFFF` 为独立 NVS
升级日志，`0x201000–0x2F0FFF` 为 960 KiB SGZ 暂存。旧升级配置
`0x200000–0x200FFF` 只读；slot1 位于 `0x300000`，不会从应用直接写当前镜像或 BOOT。
日志使用固定 48 字节小端布局和自身 CRC；当前 IDU 的 DP 参数模块还启用全局
`NVS_DATA_CRC`。IAP 模块不切换该选项，迁移已有 NVS 数据时须保持此配置一致。

解包校验 SGZ CRC、子文件 CRC、输出长度与 CRC，然后校验 MCUboot 头、
本板向量、SHA256、签名密钥哈希和 ECDSA-P256。公钥从本次 MCUboot 构建的
同一个签名密钥生成；AES 只用于旧传输兼容。全部通过后，原子保存并暂时冻结
参数写入，提交升级日志，调用 `boot_request_upgrade(BOOT_UPGRADE_TEST)`，
等待 500 ms 后复位。原健康策略连续运行 10 s 才确认试运行镜像。

掉电中断收包不会续接未知数据，状态报失败，需要重新登录。升级后读取当前
镜像哈希，且等待健康确认后才报告成功；回退到旧镜像报告失败。压缩差分包、
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
