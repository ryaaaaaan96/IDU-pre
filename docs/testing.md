# 测试、验证与资源记录

[文档导航](README.md) · [构建与升级](build-and-upgrade.md)

各日期章节记录当时的实现与验证结果，旧模块名称不代表当前目录。当前职责以
[根 README](../README.md) 和[模块接口](../modules/README.md)为准。
AIDC 产品代码、专用测试及导入工具已移除；下方日期章节保留当时的历史记录，
其中的旧构建命令和文件路径不再作为当前操作入口。

## 运行测试

从 West 工作区根目录执行：

```bash
bash applications/IDU-pre/tests/run.sh
```

脚本先运行主机 C / Python 回归，再运行 Zephyr `native_sim/native/64`。
需要 Python 环境、主机 GCC 和已准备好的 Zephyr 工作区。未激活虚拟环境时，
脚本自动使用工作区 `.venv`。输出保存在工作区 `build/IDU-pre/tests/`。

若执行环境不支持 LeakSanitizer，可仅关闭它，保留 ASan/UBSan：

```bash
CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh
```

单套测试使用独立构建目录，例如：

```bash
source .venv/bin/activate
ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
  applications/IDU-pre/tests/reference_services -d build/IDU-pre/tests/reference_services
build/IDU-pre/tests/reference_services/zephyr/zephyr.exe -flash_in_ram
```

## 覆盖入口

| 测试 | 主要内容 |
| --- | --- |
| `test_engineering.c`、`test_mcuboot_image.py` | 工程换算、签名镜像导出边界 |
| `points`、`forwarding` | IDU 点表、标准转发、NVS/事务、扩展报文及存储格式重新初始化 |
| `parameter_records` | 跨进程的新增、重排、停用及恢复参数，键冲突、格式清空与失败边界 |
| `pipeline`、`models` | 无存储数据流、模型复用、多设备及多总线隔离 |
| `dataprocess`、`collector`、`transmit` | 核心模块独立配置、采集调度、转发模式和恢复 |
| `reference_config`、`reference_dp`、`reference_ports`、`reference_services` | 原表注册、同步字节接口、CO/TR 适配、策略回调和迁移 |
| `image_confirm`、`iap`、`test_iap_codec.py` | 健康确认、独立 IAP 模块、配置驱动升级、收包/签名拒绝、SGZ/QuickLZ 边界 |
| `adc`、`test_build_configuration.py` | 标准 ADC API、采样平均、硬件超时恢复、并发隔离与 Sysbuild 配置覆盖 |
| `io` | GPIO 极性/位序/32 位边界、外部看门狗超时与 GPIO 故障锁定 |
| `vport` | 端口登记、所有权隔离、完整报文、来源保持和错误边界 |
| `communication` | UART/GPIO emulator 上的 RTU/RAW 往返、完整 ADU（含 IAPv3 大报文配置）、忙时防覆盖、来源保持、TX 超时/错误及恢复 |
| `simulator` | 独立于产品配置的双设备模拟、值/故障隔离、完整多字写入及丢失应答 |
| `acquisition` | main 启动顺序与失败短路、存储降级、采样发布、故障锁定及试运行确认条件 |

`tests/run.sh` 同时验证 Process 关闭、参数/后端可选配置，并用独立 Flash 临时文件
在新进程中执行参数、转发服务和 IAP 恢复阶段。普通运行跳过的 reboot probe 会在
相应阶段单独运行；按用例筛选运行时的其他 SKIP 不表示它们缺少全量覆盖。

本地测点直接通过现用点表验证；工程换算由 `tests/test_engineering.c` 验证。
RAW 回环和 native Flash 不能替代真实串口电气时序、NOR 掉电或 MCUboot 实际交换。

## 可选互操作与镜像检查

```bash
# 旧 QuickLZ 压缩器互操作测试
IDU_LEGACY_IAP_DIR=/path/to/io-interface-board/code/APP/Project/Iapv3 \
  bash applications/IDU-pre/tests/iap/run.sh

# 构建 SMP 固件后检查导出产物
source .venv/bin/activate
python applications/IDU-pre/tests/test_mcuboot_artifacts.py \
  build/hex/IDU-pre-mcuboot-smp
```

默认测试不需要旧源码。旧压缩器和外部 SGZ 样本相关测试在未提供输入时明确跳过；
外部 SGZ 可通过 `IDU_IAP_SGZ` 指定。固件构建/产物说明见[构建文档](build-and-upgrade.md)。

<a id="已记录验证2026-10-20"></a>

## 已记录验证（2026-10-20）

该次代码变更的验证命令（提交 `c14cc65`）：

```bash
CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --iapv3
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --aidc
```

结果对应当时的工作区，包含已有但未提交的 runtime 回归；提交源码的独立构建另列在下方。
完整测试入口通过：33 次 native simulator 执行，Ztest 汇总 189 项通过（含重启阶段等
重复执行）；Python 共 21 项，其中依赖外部旧 QuickLZ/SGZ 样本的 3 项跳过。
ASan/UBSan 保留，当前执行环境禁用 LeakSanitizer。新增服务的 11 项测试分别在
启用/关闭参数存储时通过，CO/TR 用例由 8 项增至 11 项。最后补充的迁移类型元数据修改
再次运行参数服务回归并通过。

默认、MCUboot + IAPv3、MCUboot + 大点表固件均构建通过，镜像检查与签名验证通过；
最终修改后通过 `cmake --build` 增量重建三种配置。另导出仅含本次暂存内容的源码并构建
默认固件，确认提交不依赖工作区既有的应用 runtime 重构。

### 静态 RAM 记录

| 固件配置 | 修改前静态 RAM | 修改后静态 RAM | 净变化 | 当前占 128 KiB |
| --- | ---: | ---: | ---: | ---: |
| 默认 | 80,536 B | 81,728 B | +1,192 B | 62.35% |
| MCUboot + IAPv3 | 113,328 B | 114,512 B | +1,184 B | 87.37% |
| MCUboot + 大点表 | 119,680 B | 117,088 B | −2,592 B | 89.33% |

这里包含新增服务缓冲与状态数组压缩的共同影响，不表示所有配置均净省 RAM。
`nm -S` 确认三个产品三表的 `table` 符号位于 `0x080…` 只读区；状态数组在 RAM，
默认/IAPv3 从 `0x800` 缩至 `0x600` 字节，大点表从 `0x4600` 缩至 `0x3480`。
构建前后 ELF section/symbol 清单保存在工作区
`build/IDU-pre/tests/interfaces-memory-{before,after}.json`，日志位于
`build/IDU-pre/tests/extensions-*.log`，均不提交。该次没有实板验证；运行时栈余量和
实际通信时序仍需实板确认。

## 实板待验证项

核对 20 MHz 有源时钟及 PLL/总线分频、MCUboot 跳转、PC13 看门狗交接、
DI/ADC 顺序与标定、UART 最后一个停止位之后释放 DE、SPI NOR 上电/擦写/掉电，
并验证升级中断、交换和回退。桌面结果不能作为这些项目的实物证据。
早期验证数量与容量见[迁移历史](migration-history.md)。

## 目录迁移验证（2026-10-20）

应用代码和头文件已归入 `app/`，MCUboot 配置与分区归入 `bootloader/`，
根目录 `include/`、`src/` 已移除。`sysbuild/CMakeLists.txt` 保留 Zephyr 薄入口。

完整 `tests/run.sh` 通过：33 次模拟器执行，Ztest 汇总 189 项通过（包含恢复阶段等
重复执行）；Python 21 项中 3 项因缺少外部输入跳过。该结果对应迁移后的工作区，
包含原有暂存的 runtime 改动。默认、MCUboot + IAPv3、MCUboot + AIDC 和
MCUboot + SMP UART 构建、导出与镜像验证均通过；另外独立构建仅含本次迁移的源码，
确认不依赖 runtime 暂存改动。

核对 30 个产品 C 文件，除头文件引用路径外内容不变；导入表与来源记录保持原字节。
15 份 Markdown 的本地链接/锚点及 shell 示例语法检查通过。日志位于工作区
`build/IDU-pre/tests/layout-*.log`，本次未进行实板测试。

## SoC 模块化验证（2026-10-20）

`gd32e505` 已由应用根目录移至 `modules/gd32e505`，通过 `soc_root` 注册。
PC13 看门狗浮空操作从 SoC 函数移到 MCUboot 专用的板级早期钩子。
本次按构建集成和启动边界选择验证，未重跑完整回归入口：

- 默认、MCUboot + IAPv3、MCUboot + AIDC、MCUboot + SMP UART 构建及导出检查通过，
  MCUboot 镜像验签通过。
- `tests/points` 在 `native_sim/native/64` 构建并运行通过：39 项 PASS、0 FAIL，
  单次执行跳过 1 项需要跨进程阶段的重启探针。硬件模块注册未将 ARM 源码带入主机测试。
- 使用上游 `samples/hello_world`，只指定 IDU 的 `BOARD_ROOT`、`DTS_ROOT` 和
  `EXTRA_ZEPHYR_MODULES=…/modules/gd32e505`，独立构建通过；不依赖点表模块或应用装配。
- 将本次提交范围导出为独立源码，在全新目录构建 Sysbuild，并验签应用镜像。
  MCUboot 和主应用的 `zephyr_settings.txt` 均指向导出源码的模块根，
  不依赖原来的 `SOC_ROOT` 缓存或已有的 runtime 暂存改动。
- 检查 MCUboot 链接后的 `z_cstart` 反汇编：SoC 钩子之后调用板级钩子，
  然后进入 `PRE_KERNEL_1`；主应用不启用该板级钩子。
- 文档本地链接、锚点与 shell 示例语法检查通过。

构建日志在工作区 `build/IDU-pre/tests/soc-module-*.log`，启动调用顺序在
`soc-module-startup.txt`。工作区构建包含原有 runtime 暂存改动，独立 Sysbuild 构建不包含。
时钟、Flash 参数及 PC13 浮空方式保留原值；PC13 操作从 SoC 初始化中途移动到其后，
尚未实测启动时间及外部看门狗行为。仍需验证冷启动、旧 Boot 跳转、MCUboot 交换和回退。

## ADC 与时钟配置修正验证（2026-10-20）

ADC HAL 访问已归入芯片模块驱动，应用通过标准 ADC API 读取并平均。
PLL、bypass 和 APB 分频由设备树提供；Sysbuild 保留显式指定的 MCUboot overlay。
Modbus 兼容补丁保留：4.4.2 的公开自定义功能码接口不能替代已有标准功能码、
地址过滤和接收工作队列扩展点。

```bash
CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 bash applications/IDU-pre/tests/clock/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --iapv3
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --aidc
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --smp-uart
```

- 完整回归通过：34 次模拟器运行、199 项 Ztest 通过（含恢复阶段等重复执行），
  Python 共 26 项，其中 3 项因未提供外部 QuickLZ/SGZ 输入而跳过。
- 新增 [ADC 回归](../tests/adc/README.md) 10 项，覆盖缓冲区与参数边界、
  校准/转换超时、失败恢复、并发读取隔离、通道顺序和 10 次整数平均。
  新增 Sysbuild 配置测试 5 项，覆盖默认值、自定义列表、显式空值、旧路径迁移及模块保留。
- [时钟构建检查](../tests/clock/README.md) 中 8 MHz 晶振 / 120 MHz 配置通过，
  CPU/PLL 不一致与 APB1 超频分别因预期的断言被拒绝。
- 默认及三种 MCUboot 固件构建、导出和镜像检查通过；签名验证通过。
- 独立导出本次修改范围，在全新目录进行 Sysbuild 构建并验签。
  MCUboot 设备树中的自定义 `model` 标记确认传入 overlay 生效，启动分区仍由其
  包含的产品布局提供。该构建不包含工作区原有 runtime 暂存改动。
- 文档链接、锚点与 shell 语法检查通过。

日志在 `build/IDU-pre/tests/zephyr-fix-*.log` 和 `clock-*.log`。
工作区回归包含原有 runtime 暂存内容。默认静态 RAM 为 81,768 B，比本轮前增加 40 B。
硬件替身及 ADC emulator 不验证实际校准、转换精度、采样时序或供电电压；
ADC 校准已提前到 `POST_KERNEL` 设备初始化，需在实板确认启动和看门狗行为。
本板时钟数值、通道顺序及采样次数保持不变，本轮没有进行烧录或实板测量。

## IAP 模块与配置验证（2026-10-20）

升级实现归入 [`modules/iap`](../modules/iap/README.md)，产品仅提供设备树和
Kconfig 配置。保留原线协议、结果日志内容、试运行请求及应用健康确认策略。

- `tests/run.sh` 的测试分段完成：配置测试最初因样例密钥误填为 17 字节被构建检查
  拒绝，修正为 16 字节后重跑 IAP 段并完成其余测试。排除重跑计数，共 35 次
  模拟器执行、207 次 Ztest 通过；Python 共 28 项，3 项外部样本测试跳过。
- 独立 IAP 段共 9 次模拟器执行、23 次通过，未加载 DP 或 transmit 模块。
  默认配置测试启用与 IDU 相同的 `NVS_DATA_CRC`，包含跨进程掉电／回退恢复；
  配置驱动变体关闭该全局选项并选择“无状态保存”，使用不同的 AES 密钥、
  Flash 执行地址、RAM 范围、分区大小、镜像上限和静态位图容量完成升级。
- 增加配置复制、非法分区／重叠、初始化失败后重试、回调上下文、镜像内存范围、
  输出上限及打包配置检查；3 万组畸形输入通过 ASan/UBSan。
- 默认、MCUboot＋IAPv3、MCUboot＋SMP 构建与导出通过。两个 MCUboot 配置的
  签名、HEX/BIN 一致性、工厂镜像范围及损坏镜像拒绝检查通过。真实 IAP SGZ
  解密、解包后与导出的签名 BIN 一致；该次 Python 验证 9 项，2 项旧压缩器测试跳过。
- 从本次修改单独导出源码，在新目录构建 MCUboot＋IAPv3 并验签，排除工作区
  原有暂存内容对可构建性的影响。文档链接、锚点与 shell 语法检查通过。

日志位于 `build/IDU-pre/tests/iap-module-*.log`；其中 `full.log` 保留首次运行
及配置样例拒绝记录，`final.log` 为修正后的 IAP 段，`remaining.log` 为其余回归。
工作区完整回归包含原有 runtime 暂存内容；本轮未烧录，实板 Flash 时序、
旧上位机互操作、掉电恢复与健康确认仍需验证。

## 运行层提交与 IAP 兼容目录清理（2026-10-20）

原有 16 个暂存文件经审核后提交为 `a17d53d`，保留启动／采样职责分离、
设备健康接口、故障停机策略及生命周期测试。随后删除应用层 IAP 兼容目录，
调用方直接使用 `<iap/iap.h>` 和 `iap_*`；产品开关合并到 `app/Kconfig`。

清理后 `runtime` 7 项、`mvio` 15 项、`aidc` 3 项通过；MV-IO 的独立重启探针
未在本次局部回归中运行。默认与 MCUboot＋IAPv3 构建、导出和验签通过；
默认 BIN 的 SHA256、IAP 镜像内容摘要均与清理前一致。文档链接和差异检查通过。
日志在 `build/IDU-pre/tests/iap-cleanup-*.log`。本轮未烧录或进行实板验证。

## 硬件模块与应用精简验证（2026-10-20）

硬件服务拆为 `analog_input`、`digital_io` 和 `health_monitor` 三个独立模块。
应用由 `acquisition` 管理采集状态，通过 `idu_inputs_sample()` 读取原始值，
`process/points.c` 集中点表初始化、采样映射及公共回调。产品引脚、位序和喂狗时序
来自设备树；平均次数来自 Kconfig。应用根目录仅保留入口与构建配置。

- 完整 `tests/run.sh` 通过：36 次模拟器执行，219 项 Ztest 通过（含重启阶段及
  配置变体）；Python 28 项中 3 项因缺少外部 QuickLZ/SGZ 输入跳过。
  主机 C 回归启用 ASan/UBSan，当前环境仅禁用 LeakSanitizer。
- ADC 共 13 项，覆盖统一采样、输入顺序、平均及失败质量；新增 IO 模块 8 项，
  覆盖极性、位序、32 位边界、实例隔离、GPIO 故障和看门狗超时锁定；
  采集状态机 8 项，覆盖故障阶段、无效样本发布及过期后拒绝试运行确认。
- 默认及 MCUboot＋IAPv3 构建、导出和镜像检查通过，签名验证通过。
  默认静态 RAM 为 81,768 B，IAPv3 为 114,608 B；未测运行时栈余量。
- 文档本地链接、旧代码路径引用及 `git diff --check` 检查通过。

日志位于 `build/IDU-pre/hardware-modules/`。JTAG 释放提前到板级早期初始化，
保留 SWD；应用故障停止喂狗时保持引脚输出，看门狗超时后不允许迟到进度恢复喂狗。
本轮没有烧录，需实板确认 PA15/RS485 初始化、DI/ADC 采样和 PC13 看门狗交接。

## LED、状态与统一输入模块验证（2026-10-20）

GPIO 输入与 IDU 采样实现合并到 `modules/io_input`，通用 GPIO 和 IDU 采样配置
分别通过 `DIGITAL_IO`、`IDU_INPUTS` 启用。LED 使用标准 Zephyr LED 驱动及
`modules/led_indicator`；业务阶段、故障锁定、LED 策略和健康进度归 `app/status/`。
当前为启动/故障常亮、每个正常采集周期翻转一次，尚未配置故障闪烁编码。

- 完整 `tests/run.sh` 通过：36 次模拟器执行、228 项 Ztest 通过；Python 28 项中
  3 项因缺少外部 QuickLZ/SGZ 输入跳过。主机 C 回归保留 ASan/UBSan，
  当前环境禁用 LeakSanitizer。
- 新增 LED 6 项：真实 `gpio-leds` 极性、亮灭/翻转、独立闪烁实例、取消与切换，
  包括旧回调等待互斥锁时不截断新闪烁周期，以及异步错误可观测且停止闪烁。
- 采集/status 集成共 11 项通过，覆盖 LED 初始化/输出/异步错误阻止喂狗与确认，
  指示灯错误不覆盖最初故障，以及故障后拒绝恢复正常状态。
- 输入模块的 ADC 13 项通过；移除测试工程的看门狗模块依赖后单独重跑通过，
  确认 IDU 采样可以独立于 LED、status 和看门狗使用。
- 默认、MCUboot＋IAPv3 构建、镜像导出及验签通过；最后清理头文件引用后
  再次构建通过。静态 RAM 分别为 81,880 B、114,712 B，比上轮增加 112 B、104 B。
- 文档本地链接及差异格式检查通过。

日志位于 `build/IDU-pre/status-modules/`。本轮未烧录；软件闪烁由系统工作队列
调度，需实板验证 LED 极性、工作队列负载下的闪烁和故障/看门狗复位交接。

随后将试运行健康确认改名并移至 `app/status/image_confirm.c`，状态统一由
`idu_status` 持有。确认规则不变；移动后单独重跑 11 项采集/status 集成测试与
3 项镜像确认测试通过，默认及 MCUboot＋IAPv3 再次构建、导出与验签通过。
原 `tests/boot` 改为 `tests/image_confirm`，完整回归入口已同步更新。


## 参考结构点表与直接注册验证（2026-10-20）

三张应用表改为参考结构，当时默认值另存 `defaults.c`，共享 const 型号表通过模块的
临时执行视图读取。删除 `point_metadata` 和字符串回调分发，产品入口只注册回调，
`dp_init()` 负责通用校验和数据池初始化。质量寄存器、样本年龄过期和旧 NVS 迁移暂缓。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 全部通过：
  36 次模拟器执行、229 项 Ztest PASS；Python 29 项中 26 项通过、3 项因缺少外部输入跳过。
  主机 C 回归保留 ASan/UBSan。
- AIDC 4 项和 reference_config 1 项通过：直接版本/协议回调、
  未实现业务拒写且参数不变、FC03 非持久点仅更新 RAM、UPP 模型结构和周期策略校验、临时视图下默认覆盖按键隔离。
- `./build.sh`、`./build.sh --mcuboot --iapv3`、`./build.sh --mcuboot --aidc`
  均构建通过，默认镜像布局检查及 MCUboot 签名/导出检查通过。
- 当前静态 RAM 分别为 82,088 B、114,920 B、118,024 B。默认和 IAPv3 比上一轮
  各增加 208 B，设备装配与逐行采集调度仍占 RAM；没有声称整机净节省。
- `nm -S` 验证原处理/采集/转发表、默认数组及共享 DSP 表位于 `0x080…` 的只读区，
  模块不分配整套 native 点表副本。
- 导入工具用仓库中的来源快照验证标记区可复现。原 AIDC 来源目录当前不可用，
  未执行从该外部源码重新解析；来源文件哈希保持原记录。

本轮没有烧录或实板测量；UART 帧数从合并块改为逐行请求，实际总线负载、启动耗时、
运行时栈余量及 Flash 掉电行为仍需实板验证。旧 schema 不匹配时拒写，不自动格式化。

配置目录随后整理为 `cfg/device.c`、`cfg/inc/*.h`、`cfg/tables/*.c`；构建引用、
导入输出、快照复现测试和维护文档一并更新。

## 默认值合并及国家参数查询验证（2026-10-20）

删除 `defaults.c`，原默认值、上下限和恢复策略逐行保持不变，合并到
`process.c` 的单一 const `parameters[]`。新增按国家查询接口，当前只有通用配置，
DP 回调继续读取通用参数，未接入运行期国家选择。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  36 次模拟器执行、232 项 Ztest PASS；Python 29 项中 26 项通过，
  3 项因缺少外部输入跳过；主机 C 保留 ASan/UBSan。
- 新增验证覆盖只读查询不改变当前值及 NVS 代次、未知国家/设备/信号拒绝、
  空指针及失败输出保护、有符号范围和数组初值、DSP 实例复用及恢复策略。
- 导入测试验证 `process.c` 两个标记区一并更新，保留标记外的手写国家配置；
  仓库来源快照仍可复现全部导入行，未重新读取外部 AIDC 源码。
- `CCACHE_DISABLE=1 ./build.sh` 和 `CCACHE_DISABLE=1 ./build.sh --mcuboot --aidc`
  通过，包括默认布局检查及 MCUboot 签名验证。
- 默认固件 Flash/RAM 为 94,164 / 82,088 B；大点表固件为
  107,672 / 118,024 B。RAM 与合并前相同；`nm -S` 确认大点表的
  `parameters[]` 为 `0x08024b3c` 的只读符号。

具体国家参数尚未提供，本轮不验证国家业务值或国家切换；未做实板验证。

## 设备表直接关联验证（2026-10-20）

`device.c` 改为静态 const 设备表，显式填写每个设备的处理/协议表指针及数量；
getter 只返回表，删除按设备号补挂表的装配逻辑。三表由 `device.c` 包含，
保持分文件维护且只编译一次。设备实例与其关联配置统一在 `devices[]`，
同型号参数按共用处理表查找首个实例，当前参数来源及设备顺序保持不变。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  36 次模拟器执行、232 项 Ztest PASS；Python 26 项通过、3 项缺少外部输入跳过。
  包含参数重启恢复、默认值/范围查询、8 个 DSP 实例采集和来源快照复现。
- 默认固件及 `--mcuboot --aidc` 构建通过，默认布局和 MCUboot 签名验证通过。
- 默认 Flash/RAM 为 94,044 / 81,960 B；AIDC 为 107,504 / 117,320 B。
  相比本轮修改前，RAM 分别减少 128 B 和 704 B。
- `nm -S` 确认应用设备配置为只读符号：默认设备表位于 `0x08017d70`，
  大小 128 B；AIDC 位于 `0x08024a58`，大小 704 B。运行状态仍由模块保存在 RAM。
- 导入工具同步生成完整设备关联及 UPP 字段总数；使用现有来源快照验证，
  未重新解析不可用的外部 AIDC 源码。

本轮未做实板验证。

## 点表独立编译验证（2026-10-20）

三表恢复独立编译，`device.c` 通过 `inc/tables.h` 的 `extern const` 数组声明引用，
设备表仍在编译期完成关联。数量使用枚举，定义处以 `BUILD_ASSERT` 校验实际长度；
导入工具同步更新数量枚举，默认值、范围、信号顺序及回调保持不变。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  36 次模拟器执行、232 项 Ztest PASS；Python 30 项中 27 项通过，
  3 项因缺少外部输入跳过。主机 C 保留 ASan/UBSan，禁用 LeakSanitizer。
- 导入测试 12 项通过，覆盖新增数量头文件的输出、报告路径冲突，以及删减本地点、
  DSP 点、未解析映射和转发项时数量同步更新；来源快照可复现当前标记区。
- 默认固件及 `--mcuboot --aidc` 构建、默认镜像布局检查和 MCUboot 验签通过。
  Flash/RAM 分别为 94,060 / 81,960 B、107,520 / 117,320 B；RAM 与拆分前相同。
- 使用上述两个构建的实际编译参数，关闭 `CONFIG_IDU_COLLECT_VIRTUAL`，
  分别编译检查 `device.c` 和三表，共 8 项通过。此项仅验证配置源码分支，
  不代表已验证物理 RTU 端口或完整 RTU 固件。
- 对 8 个数量枚举各注入 `-1`、`+1` 偏差，16 项均在编译期被断言拒绝；
  使用临时头文件，未修改产品配置。
- `nm -S` 确认大点表固件的设备表、导出的 const 点表及默认参数均在
  `0x080…` 只读区，设备表为 704 B。

日志位于 `build/IDU-pre/table-linkage-checks/`。导入验证使用仓库已有来源快照，
未重新读取不可用的外部 AIDC 源码；本轮未做实板验证。

## 虚拟端口核心验证（2026-10-20）

新增独立 `modules/vport`，采用启动期注册链路、逻辑端口与路由的方式。
核心仅依赖 Zephyr 内核，CAN/UART/RS485/socket 实物后端及原 CO/TR 迁移尚未接入。

- `tests/vport` 仅通过 `ZEPHYR_EXTRA_MODULES` 加载该模块，13 项 Ztest 通过，
  覆盖 CAN 标准/扩展 ID 分流、FD 长度校验、多链路汇入与原路回复、
  peer/方向限制、流来源隔离、ISR 入队、接收副本、背压与丢失序号、
  物理发送锁/事务锁共享、后端错误不重试，以及配置冲突拒绝。
- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  37 次模拟器执行、245 项 Ztest PASS；Python 30 项中 27 项通过，
  3 项因缺少外部 QuickLZ/SGZ 输入跳过。主机 C 保留 ASan/UBSan，禁用 LeakSanitizer。
- `CCACHE_DISABLE=1 ./build.sh` 通过，默认镜像布局检查通过。
  vport 默认关闭，固件 Flash/RAM 仍为 94,060 / 81,960 B，
  二进制 SHA256 与增加模块前一致。
- 单独目录 `build/IDU-pre/vport-enabled` 使用
  `west build -b idu_pre applications/IDU-pre -d build/IDU-pre/vport-enabled -- -DCONFIG_VPORT=y` 完成 ARM 构建，
  确认核心源码可交叉编译；应用尚未注册配置，不代表已启动或实测虚拟端口。
- 文档链接与 `git diff --check` 通过。

日志位于 `build/IDU-pre/vport-validation/`。未烧录验证，模拟 CAN 元数据路由
不能证明 GD32E505 的 CAN 驱动、收发器或实际总线时序已经可用。

## 通信接入与硬件模块整理（2026-10-20）

现有 RS485 转发和虚拟采集通过 `transmit` 的 Modbus 适配接入 vport，
配置见[通信端口接入](communication-ports.md)。`gpio`、`watchdog` 分别替代
原输入、健康监控模块的名称；实现逻辑保持不变。LED 模块及通用闪烁任务已删除，
由 `app/status/status.c` 直接调用 Zephyr LED API。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 全量通过：
  39 次模拟器执行、258 项 Ztest PASS（含恢复阶段等重复执行）；
  Python 30 项中 27 项通过、3 项缺少外部 QuickLZ/SGZ 输入而跳过。
  主机 C 保留 ASan/UBSan，仅关闭 LeakSanitizer。
- `communication` 普通配置和 IAPv3 大报文配置各 9 项通过，覆盖完整 RTU/RAW
  报文、CRC 元数据、收发错误、接收占用、交接超时、DE 释放及配置恢复。
- `io` 8 项通过；`acquisition` 11 项通过，验证状态灯亮灭/翻转、
  首个故障保留，以及 LED/看门狗错误阻止喂狗和镜像确认。
- 默认、MCUboot + IAPv3、MCUboot + AIDC 构建和镜像检查通过，
  两种 MCUboot 应用镜像验签通过。

| 配置 | Flash | 静态 RAM |
| --- | ---: | ---: |
| 默认 | 97,928 B | 85,832 B |
| MCUboot + IAPv3 应用 | 120,988 B | 121,928 B |
| MCUboot + AIDC 应用 | 111,388 B | 121,192 B |
| MCUboot | 36,632 B | 18,096 B |

日志位于工作区 `build/IDU-pre/vport-integration/`。本轮没有烧录或实板测量；
UART/GPIO emulator 验证不代表实际 RS485 电气时序、栈余量或 CAN 已通过验证。

## 通信职责归入模块（2026-10-20）

删除 `app/communication`，产品端口、协议、命令和仿真数据以常驻配置放入
`app/cfg/`；业务参数转换保留在 `app/process/serial_settings.c`。
`transmit` 管理各转发口的生命周期、寄存器服务和 MV-IO 协议，
`collect` 管理采集初始化、可选寄存器模拟器和模型默认值后端。
模块不引用 IDU 头文件、设备树别名或产品 Kconfig。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 全量通过：
  40 次模拟器执行、268 项 Ztest PASS（包含恢复阶段重复执行）；
  Python 30 项中 27 项通过、3 项缺少外部 QuickLZ/SGZ 输入而跳过。
- 新增独立模拟器套件 4 项，验证双设备数据/故障隔离、多字写入原子性、
  拆分写拒绝、丢失应答不重试及无效布局拒绝。测试不加载应用配置或业务代码。
- 双转发口经 `tr_server_service()` 启动，新增地址连续更新、端口隔离及
  重复接口配置拒绝测试；新增网络注册幂等、MV-IO 状态寄存器及配置边界测试。
  命令业务错误不会被误判为“未处理请求”而继续执行普通参数写入。
- 既有参数保存/恢复、MV-IO 广播/247 地址、AIDC 模型及串口参数测试通过；
  三张点表、设备表、默认值和 NVS schema 未修改。
- 默认、MCUboot + IAPv3、MCUboot + AIDC 最终构建与导出检查通过，
  MCUboot 应用镜像验签通过。文档本地链接和 `git diff --check` 通过。

| 配置 | Flash | 静态 RAM |
| --- | ---: | ---: |
| 默认 | 99,484 B | 85,928 B |
| MCUboot + IAPv3 应用 | 122,496 B | 122,024 B |
| MCUboot + AIDC 应用 | 112,676 B | 121,240 B |

日志在工作区 `build/IDU-pre/communication-ownership/`。本轮未烧录验证，
未验证真实双 UART 采集、CAN 收发或实板栈余量。

## 启动与应用职责整理（2026-10-20）

整机启动顺序集中在 `app/main.c`，不设置独立的 startup 文件；
`acquisition` 只执行采样周期，`process` 只保留测点业务，Flash/参数就绪条件
归 `status`。移除旧设备服务包装接口，三类点表模块按启用条件创建库。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  40 次模拟器执行、270 项 Ztest PASS（含恢复阶段重复执行）；
  Python 30 项中 27 项通过、3 项因缺少外部 QuickLZ/SGZ 输入跳过。
  主机 C 保留 ASan/UBSan，仅禁用 LeakSanitizer。
- 启动/采集套件 13 项通过，直接编译应用 `main.c` 并替换模块服务：
  检查注册、总线建立、参数恢复和轮询的顺序，以及每个必要步骤失败后
  停止后续启动；存储失败保留采样但不确认镜像，待恢复事务清除后才允许确认。
- 默认、MCUboot + IAPv3、MCUboot + AIDC 构建及导出检查通过，
  升级镜像验签通过。三种应用静态 RAM 均与整理前一致。
- 构建/完整回归日志没有编译警告、错误或空模块库警告；
  225 个本地 Markdown 链接及 `git diff --check` 通过。

| 配置 | Flash | 静态 RAM |
| --- | ---: | ---: |
| 默认 | 99,540 B | 85,928 B |
| MCUboot + IAPv3 应用 | 122,552 B | 122,024 B |
| MCUboot + AIDC 应用 | 112,740 B | 121,240 B |

日志在工作区 `build/IDU-pre/code-cleanup/`。本轮未烧录或进行实板验证。

## 初始化保护、状态快照与资源裁剪（2026-10-20）

转发适配器在改写回调前检查接口是否已占用；初始化失败归还新增工作队列槽，
初始化期间收到的 RX 通知在队列就绪后处理。参数状态寄存器在同一 FC03 响应中
共享一次快照。DP 按设备查找处理表，等待采样完成改为条件变量广播，
保留点表顺序及持久化布局。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  40 次模拟器执行、275 项 Ztest PASS（含恢复阶段重复执行）；
  Python 33 项中 30 项通过、3 项因缺少外部 QuickLZ/SGZ 样本跳过。
  主机 C 保留 ASan/UBSan，仅关闭 LeakSanitizer。
- 新增回归覆盖重复初始化后原写回调及其他接口仍有效、失败初始化不耗尽槽位、
  启动期 RX 通知延后处理、参数序号跨 16 位进位时快照一致，以及相同/不同票号
  多等待者、超时后继续处理和无序设备/信号表查找。
- 完整应用所需服务由实际 CMake 检查，Python 覆盖默认、RTU 和 AIDC 条件。
  独立运行 `west build --cmake-only ... -- -DCONFIG_TR_SERVER_SERVICE=n`
  验证配置阶段按预期拒绝，报告 `IDU-pre requires CONFIG_TR_SERVER_SERVICE=y`。
- 默认、MCUboot + IAPv3、MCUboot + AIDC 构建及导出检查通过，
  两种 MCUboot 应用镜像验签通过。全量回归及三种构建无编译警告或错误。
  本地 Markdown 链接和 `git diff --check` 通过。

| 配置 | Flash | 静态 RAM | 相比上一轮 RAM |
| --- | ---: | ---: | ---: |
| 默认 | 100,008 B | 76,032 B | −9,896 B |
| MCUboot + IAPv3 应用 | 123,012 B | 108,744 B | −13,280 B |
| MCUboot + AIDC 应用 | 113,384 B | 121,272 B | +32 B |
| MCUboot | 36,632 B | 18,096 B | 不变 |

默认及 IAPv3 裁剪一个未使用采集总线槽和一个独立转发槽；AIDC 本就只有一个
转发槽，并保留两个采集总线槽。每个线程的栈大小不变；新增等待和启动状态有少量
静态开销，因此 AIDC 增加 32 B。以上是链接期数据，不代表运行时栈余量。

日志在工作区 `build/IDU-pre/reliability-fixes/`。本轮未烧录或进行实板验证。

## 采集与转发线程配置归属（2026-10-20）

`CO_MAX_BUSES` 替代 `DP_MAX_BUSES`，容量检查移入 `co_init_bus()`；
采集数量/栈大小归 collect Kconfig，隔离转发数量/栈大小及新增的
`TR_CONFIG_STACK_SIZE` 归 transmit Kconfig。当前 IAPv3 的数值在 `prj.conf`
手动维护，固件类型配置不再自动覆盖；多端口测试显式声明自己的容量。

- 最终 `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash tests/run.sh` 通过：
  40 次模拟器执行、275 项 Ztest PASS；Python 33 项中 30 项通过，
  3 项因缺少外部 QuickLZ/SGZ 样本跳过。主机 C 保留 ASan/UBSan。
- 单总线采集测试增加容量边界检查：配置表引用超容量总线、注册超容量 ID
  均拒绝；随后有效注册成功，失败路径不安装错误设备配置。
- 独立变体把采集栈改为 5120 B，5 项采集测试通过；双转发口栈改为每份
  5120 B、共享重配置栈改为 3072 B，8 项转发测试通过。
  ELF 符号分别为 5120 B、10240 B、3072 B，证明 Kconfig 值实际控制分配。
- 最终 `./build.sh --mcuboot --iapv3` 构建、镜像验签和 SGZ 导出通过；
  生成配置确认 MCUboot 和 IAPv3 均启用。应用 Flash 为 123,036 B，
  静态 RAM 为 108,744 B，与本轮前 RAM 一致。
  ELF 中采集栈 4096 B、转发栈 4096 B、共享重配置栈 2048 B。
- 最终回归及 IAPv3 构建无编译警告或错误。本地文档链接及
  `git diff --check` 通过。

日志及 ELF 栈尺寸记录位于工作区 `build/IDU-pre/stack-config/`，
最终 IAPv3 配置记录为 `iap-final-resources.json`。本轮未进行实板栈余量测量。

## 2026-10-20 清理停用协议与启动路径

移除 MV-IO 特殊路由、窗口白名单、补零点、重复别名、旧 BOOT 构建与检查器、
旧 Flash 区域声明及旧路径自动迁移。现用 dp_/co_/tr_ 接口和三表结构继续使用；
持久化参数的键、类型、顺序及存储地址未改动。
IAPv3 通过 `tr_register_service.extension` 接入，能力和版本寄存器归 IAP；
普通转发按连续地址处理，不再提供 247 特殊入口、跳空洞读或无效写 ACK。

- `tests/run.sh` 通过：40 次 native_sim 执行、275 项 Ztest PASS；
  Python 34 项中 31 项通过，3 项外部 QuickLZ/SGZ 互操作测试缺少样本而跳过。
  主机 C 使用 ASan/UBSan；受执行环境限制，设置 `ASAN_OPTIONS=detect_leaks=0`。
  编译缓存放在工作区 `build/.ccache/`。
- `tests/forwarding` 替代旧协议套件，覆盖未映射地址、连续字段读取、保存失败异常、
  广播无副作用、重启恢复、状态快照和故障恢复；验证完整 1 KiB 扩展报文先于
  标准 Modbus 长度检查进入回调，取消扩展后恢复普通校验。
- IAP 原生测试覆盖能力/版本查询，以及现有分包、重启日志和配置变体。
  AIDC 删除一条未使用补零点后共 552 个处理点，原业务表回归通过。
- `./build.sh` 默认 MCUboot + IAPv3 和 `./build.sh --no-upload` 均构建、验签成功。
  IAPv3 应用 Flash 121,908 B、RAM 108,744 B；无上传应用 Flash 100,900 B、
  RAM 76,032 B；MCUboot Flash 36,584 B、RAM 18,096 B。
- `test_mcuboot_artifacts.py` 对两套实际产物检查签名、HEX/BIN、工厂镜像范围，
  每套的 5 个损坏／错误密钥拒绝用例均通过。文档本地链接和 `git diff --check` 通过。

日志位于工作区 `build/IDU-pre/cleanup/`。没有执行实板烧录；RS485 时序、
上位机 IAP 互操作、掉电交换和看门狗交接仍需实物验证。


## RAW 报文端口迁移验证（2026-10-21）

CO/TR 的 `src/message.c` 已通过通用回调端口收发；协议各自运行 RAW，
物理串口复用 Zephyr RTU 组帧和 DE。旧路由表、vport RX 线程及 ADU 交接路径已删除。

- `tests/run.sh` 完整通过：43 次 native_sim 执行，273 项 Ztest PASS，0 FAIL，0 编译警告。
- Python 共 34 项，31 项通过；3 项因外部旧 QuickLZ/SGZ 样本缺失而跳过。
- `collect_modbus` 实际经过 CO 消息入口及 RAW 回调，未启用 transmit 或 Modbus 服务端；
  `transmit_port` 实际经过 TR 消息入口，未启用 collect 或 Modbus 客户端。
- `communication` 普通及 1100 字节配置各 9 项通过，覆盖完整 RTU 往返、CRC、
  有效 CRC 前缀的超长帧拒绝、来源保持、忙时防覆盖、注册失败后重试、
  TX 超时释放 DE、静默请求恢复，以及请求结束后再调整串口参数。
- MCUboot + IAPv3 构建、签名验证及 SGZ 导出通过。应用 Flash 123,972 B、RAM 104,576 B；
  相比上一轮协议目录整理的 RAM 108,736 B，链接报告减少 4,160 B。

日志位于工作区 `build/IDU-pre/raw-migration/`。主机测试使用
`ASAN_OPTIONS=detect_leaks=0` 规避运行环境的 LeakSanitizer 限制，ASan/UBSan 保持启用。
未烧录或测量实板；UART/GPIO emulator 不代表实物 DE、电气边沿和帧间隔验证。


## 移除产品模拟从机与同线 RS485 联调（2026-10-21）

`app/cfg/simulation.c` 已移至 `tests/fixtures/`，产品只绑定两个 RAW 逻辑口，
共用 USART2/RS485。AIDC 虚拟模型配置移至 `tests/aidc/tables.conf`，仅用于回归。

- `ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh` 通过：
  44 次 native_sim 执行，279 项 Ztest PASS，0 FAIL，0 编译警告。
- Python 共 33 项，30 项通过；3 项因缺少外部旧 QuickLZ/SGZ 样本跳过。
- 新增 `tests/shared_rs485` 共 6 项：实际产品 cfg 编译且禁用 `CO_SIMULATOR`，
  外部从机夹具经 UART emulator 回应；覆盖采集与缓存读、同线北向写/南向回读、
  冲突地址拒发、南向事务期间北向帧不误判为应答、异常/坏帧/超时恢复、周期线程持续采集。
- 既有通信普通帧和 1100 字节配置、IAP、参数持久化/重启恢复回归继续通过。
- `./applications/IDU-pre/build.sh` 默认 MCUboot + IAPv3 构建、签名验证及 SGZ 导出通过。
  应用 Flash 120,400 B、RAM 101,984 B；相比上一轮 RAM 104,576 B 减少 2,592 B。
  `.config` 确认无 `CO_SIMULATOR` / `CO_MODEL_SIMULATOR`，RAW 接口和 vport 容量均为 2。

日志位于工作区 `build/IDU-pre/shared-rs485/tests-final.log` 和 `build-final.log`。
实板尚未烧录；模拟器结果不代表 DE 时序、吞吐量或外部多主碰撞率。
同线测试中南向地址保持 1，IDU 拨码需使用不同地址，例如 2；外部主站留出采集窗口。
既有 31500–31502 → 40000–40002 演示参数映射保留为实际下发配置，
其持久化键不变；连接真实设备时必须按设备协议调整这些地址。


## 协议目录、分散登记与下发标志解耦（2026-10-21）

`app/cfg` 改为 `app/protocol`，设备、转发服务和参数命令配置合入 `protocol.c`。
`inc/` 只保留对外头文件，内部头文件位于目录根部。三表独立编译，使用
Zephyr Iterable Sections 按整表登记，DP 按标识校验并装配设备关联。

- `bash applications/IDU-pre/tests/run.sh` 通过：45 次 native_sim 执行，
  288 项 Ztest PASS、0 FAIL、0 编译警告。包括跨进程的 NVS 重启恢复验证。
- Python 共 33 项，30 项通过；旧压缩器互操作的 2 项未配置外部源码，
  另 1 项未指定对应测试镜像的 SGZ 样本，按原测试条件跳过。
- 新增 `tests/table_registry` 的 9 项覆盖跨文件发现、重复/缺失/错误类型绑定，
  不落盘且不读回的下发、失败不重放、可写点轮询、失败质量、持久目标与反馈分离，
  混合保存策略拒绝，以及存储故障时非持久命令仍能执行。
- 共享 RS485 的 6 项回归继续通过；产品周期点缩减为 DI、模拟量 1/2，
  协议号和地址仅初始化读取。现有持久参数键和映射保留。
- MCUboot + IAPv3 构建、签名验证及 SGZ 导出通过。
  应用 Flash 121,620 B、RAM 102,112 B；较上一轮 RAM 增加 128 B，
  对应两个 DEV_TABLE_S 指针关联视图。nm 确认点表、登记项与绑定声明在 Flash 的 r 区，
  没有将点表内容复制到 RAM。

日志位于工作区 `build/IDU-pre/protocol-refactor/tests.log` 和 `build.log`。
本地模拟量仍使用原电流换算；本轮未烧录实板，不代表总线性能、电气时序或真实掉电验证。

## 移除 AIDC 产品分支（2026-10-21）

应用只保留 IDU 点表与业务；IAPv3 仍是独立升级模块。删除 AIDC 专用测试及导入工具
回归，通用模型、DP/CO/TR、IAP 和产品通信测试继续运行。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`
  通过：44 次 native_sim 执行，283 项 Ztest PASS、0 FAIL，包含独立进程的参数重启恢复。
- Python 共 21 项，18 项通过；2 项缺少外部旧压缩器源码，1 项缺少外部 SGZ 样本，
  按原测试条件跳过。主机工程换算 ASan/UBSan 检查通过。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --pristine always` 通过，
  IDU 固件启用 MCUboot 与 IAPv3，签名验证及 SGZ 导出通过；构建和测试均无编译警告。
- 清理后的 8 个 IDU 配置数组与旧代码中实际使用的 IDU 分支逐字一致：
  处理表、参数表、采集表、转发表、设备绑定、命令和状态寄存器均未改动。
- 应用和 MCUboot 的生成设备树中已没有 AIDC 专用分区；IDU NVS、IAP 与升级分区
  地址保持不变。删除分区声明不会擦除旧数据。
- IDU 应用 Flash 121,596 B、RAM 102,112 B；MCUboot Flash 36,568 B、RAM 18,096 B。

日志位于工作区 `build/IDU-pre/remove-aidc/tests.log` 和 `build.log`。
本轮未烧录实板；通用模型测试仅验证模块接口，不代表实现了 UPP 线协议。

## 2026-10-21 状态与命令回归处理点表

按参考工程的 `DM_SIG_CFG_S.pf_sig_proc` 组织状态和命令：
32000–32003 为一个带 `FLAG_READ_CALLBACK` 的四字点，31090–31093 为非持久命令点。
命令回调通过 `dp_defer_para_action()` 登记动作，DP 完整校验后同步执行，
不提交触发值、不自动重放；TR 只保留地址映射、地址段回调和完整 ADU 扩展。
决策见 [ADR 0002](adr/0002-process-point-callbacks.md)。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`
  通过：44 次 native_sim 执行、288 项 Ztest PASS；Python 21 项中 18 项通过，
  3 项外部 QuickLZ/SGZ 互操作测试缺少样本而跳过。主机工程换算使用 ASan/UBSan。
- 新增/调整覆盖：设置与 INIT/GET 的执行时机，单字 FC16 命令、批量命令拒绝，
  回调或最终范围校验失败时不执行动作，派生写入拒绝，执行错误转换为 Modbus 异常，
  命令不缓存触发值；状态完整/部分读取只取一次快照、实时更新和写入拒绝。
  地址段回调、普通点表回退、IAP 大包与广播入口继续通过。
- 参数持久化、意图恢复、重启探针及掉电注入回归通过。新状态/命令不带
  `FLAG_PERSISTENCE`，原五个持久参数的键、类型、相对顺序及分区未改动。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh` 通过，MCUboot 与 IDU 镜像
  验签成功，导出 IAPv3 SGZ。IDU Flash 121672 B、RAM 102112 B；
  MCUboot Flash 36568 B、RAM 18096 B。最终配置 `CONFIG_IDU_IAPV3=y`。
- 构建/测试无编译警告或错误；修改文档的本地链接与 `git diff --check` 通过。

日志保存在工作区 `build/IDU-pre/process-callbacks/`，不纳入源码仓库。
本轮未进行实板 Flash 掉电、RS485 性能或 DE 时序验证。

## 2026-10-21 IAPv3 迁移到协议地址段

IAPv3 从全局 `extension` 改为本地设备的 `PRO_ADDR_SLAVE` / `MB_TABLE_SEG_T`。
协议段表与登记项在 Flash，回调继续使用 Zephyr 完整 ADU；普通点表与标准透传段
保留原校验规则。决策见 [ADR 0003](adr/0003-iap-protocol-segments.md)。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`
  通过：45 次 native_sim 执行、298 项 Ztest PASS、0 FAIL。Python 共 21 项，
  18 项通过，3 项因缺少外部 QuickLZ/SGZ 样本而按原条件跳过；主机工程换算
  ASan/UBSan 检查通过。
- 地址段回归覆盖：首尾地址命中、段外地址、跨段变长载荷、完整 1029 字节 ADU、
  单元地址过滤、显式广播允许/禁止、段外广播不写参数、命中错误不回退到 writer；
  空表/回调、倒置区间、重叠和重复协议组均拒绝，失败保留已安装路由。
- 新增 `tests/iap/routed.conf`：真实 IAP 状态机经 TR 协议段入口运行，覆盖能力、版本、
  登录、1 KiB 包、广播写、乱序补包、元数据原子校验、超时、Flash 故障、验签和升级。
  该变体不启用 DP 处理线程、参数存储或 collect。原独立 IAP 及配置式 IAP 测试继续通过。
- Iterable Sections 的设备装配支持 `PRO_ADDR_SLAVE`；协议段不增加 DP 点数、
  普通转发数或轮询数。未启用 IAP 的产品点表/参数转发及标准段测试通过。
  原五个持久参数的键、类型、顺序与 NVS 分区不变。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh` 通过；最终启用
  `CONFIG_IDU_IAPV3=y`，MCUboot/IDU 镜像验签与 IAPv3 SGZ 导出成功。
  IDU Flash 121960 B、RAM 102112 B；MCUboot Flash 36568 B、RAM 18096 B。
  `nm` 确认产品协议段数组、登记项和服务配置均在 Flash 的只读区。
- 构建/回归未出现编译警告或错误；文档本地链接及 `git diff --check` 通过。

日志保存在工作区 `build/IDU-pre/iap-segments/`。本轮未进行实板 RS485 升级、
物理 DE 时序或真实掉电验证。

## 2026-10-21 默认值和范围内嵌处理表

处理表行直接包含 `DM_SIG_DATA_S data`，删除 IDU 的 `point_parameter`、
`parameters[]`、行宏和默认/范围/恢复查表桥接。DP 直接读取 Flash 配置，
`FLAG_NO_RESTORE` 与 `FLAG_RANGE_CHECK` 统一放在 `sig_flag`；没有独立恢复策略字段。
决策见 [ADR 0004](adr/0004-inline-process-parameters.md)。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`
  通过：47 次 native_sim 执行、310 项 Ztest PASS、0 FAIL。Python 共 21 项，
  18 项通过，3 项因缺少外部 QuickLZ/SGZ 样本按原条件跳过；主机工程换算 ASan/UBSan 通过。
- 新增 `tests/point_parameters` 通用/国家两种初始化测试，共 12 次测试通过：
  国家只读查询、缺项回退、共享处理表、未知国家、失败输出不变、注册冻结、
  数组零默认、显式数组字序、有符号范围、主动恢复开关、NVS 优先及 RAM 动态覆盖。
  `FLAG_RANGE_CHECK` 开启且双零时只允许零；未开启时填写的范围不生效，使用类型范围。
- 旧默认/范围回调的独立适配、状态/命令回调、南向下发、持久化重启、
  未决事务恢复和 IAPv3 协议段回归继续通过。国家参数选择固定在初始化前，
  当前产品仍为 COMMON，未加入具体国家参数或自动覆盖已保存值的行为。
- 五个持久参数的键、类型、宽度、顺序和派生存储标志保持不变；
  新范围检查标志不进入 NVS schema 签名，未引入存储格式迁移。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh` 通过；最终启用 MCUboot/IAPv3，
  镜像验签及 SGZ 导出通过。IDU Flash 121836 B、RAM 102120 B；
  MCUboot Flash 36568 B、RAM 18096 B。
  相比 `acef3be`，IDU Flash 减少 124 B，RAM 增加 8 B，来自国家选择及覆盖回调状态。
  `nm` 确认处理表和零默认数组在 Flash 的只读区，没有将参数配置复制到 RAM。
- 构建与回归无编译警告或错误；文档本地链接、脚本语法及 `git diff --check` 通过。

最终日志保存在工作区 `build/IDU-pre/process-parameters/tests.log` 和 `build.log`。
本轮未进行实板 Flash 掉电、RS485 升级或总线时序验证。

## 2026-10-21 点表字段显式初始化与模块默认值

处理表逐个写出六个成员及 `data` 的三个值，采集表逐个写出全部十个成员；
转发表和地址段表原本已完整填写。没有专用配置的字段分别使用 DP、collect、transmit
自己的默认常量，不采用整行宏，不增加 RAM 默认模板或初始化流程。

- 字段检查通过：33 条处理行、7 条采集行、32 条转发行、3 条协议段全部具备完整字段。
- `points`、`forwarding`、`point_parameters` 的 native_sim 回归通过，分别为
  46、23、6 项 Ztest PASS，共 75 项，0 FAIL。常规运行的两项独立重启探针按原条件跳过，
  本轮未重跑独立进程重启测试或完整回归；上轮完整回归结果见前一节。
- IDU MCUboot/IAPv3 构建、镜像验签及 SGZ 导出通过，无编译警告或错误。
  Flash 121836 B、RAM 102120 B，与 `4877e70` 相同。
- 与修改前的 ELF 对比，两个处理数组、采集数组、转发数组和 IAP 协议段数组的
  长度及内容 SHA-256 全部相同；完整未签名 `IDU-pre.bin` 的 SHA-256 也相同。
  证明此次只是将隐式字段和通用字序显式写为模块默认值，没有改变编译后的配置或行为。
- 文档链接与 `git diff --check` 通过。日志及比较基线保存在工作区
  `build/IDU-pre/explicit-table-defaults/`，不提交。

本轮未进行实板验证。

## 2026-10-21 设备、服务和端口配置显式初始化

继续将逐字段初始化规则应用到设备挂表、处理启动计划、采集总线、转发服务和
vport 配置。缺省常量分别由 dataprocess、collect、transmit、vport 提供；
应用明确填写实际标识、地址和时序，不新增硬件选择或未来端口预留。
Zephyr Modbus 联合体仅初始化当前 RAW 客户端或服务端使用的成员，
运行态锁、定时器和服务状态仍由各模块的生命周期接口初始化。

- `table_registry`、`shared_rs485`、`acquisition`、`forwarding` 的 native_sim
  回归通过，分别为 9、6、13、23 项 Ztest PASS，共 51 项，0 FAIL。
  覆盖设备装配、启动计划、共享 RS485 和转发恢复；常规运行的独立重启探针
  按原条件跳过，本轮未重跑完整回归或独立进程重启测试。
- MCUboot + IDU/IAPv3 构建、两份镜像验签及 SGZ 导出通过，无编译警告或错误。
  IDU Flash 121836 B、RAM 102120 B；MCUboot Flash 36568 B、RAM 18096 B，
  与修改前相同。
- 未配置北向物理端口别名时，转发服务的接口名仍为 NULL，服务保持禁用；
  RAW 服务所需站号和发送回调由生命周期接口安装。
- 修改文档的本地链接与 `git diff --check` 通过。日志保存在工作区
  `build/IDU-pre/explicit-service-defaults/`，不提交。

本轮未进行实板验证。

## 2026-10-21 Zephyr 点值分配、双设备与发送身份

DP 点值改为启动时通过 `k_malloc()` 按实际字数一次申请，配置上限仍由
`DP_POINTS_VALUE_WORDS` 校验。失败不安装配置，申请后初始化失败释放内存并允许重试；
运行期间不分配或释放点值。产品的 DP 系统堆预算在 `prj.conf` 手动设置为 256 B。

`protocol.c` 使用 `LISTIFY(2, ...)` 生成设备 10、11，端口 0，站号 1、2，
并逐项注释宏参数。两台共用只读 process/collect 表，保留独立值与设备状态。
CO 后端回调和 Modbus 事务接口显式接收 `dev_id`；vport 来源结构增加同名字段，
保留独立的 64 位 `peer`。每个 RAW 接口保存同步事务身份，发送侧传入
`destination.dev_id`，接收侧校验当前身份，TR 响应保留完整来源。

- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`
  最终完整执行通过：48 次 native_sim、316 项 Ztest PASS、0 FAIL。
  Python 共 21 项，18 项通过，3 项因缺少外部 QuickLZ/SGZ 样本按原条件跳过；
  主机工程换算 ASan/UBSan 通过，构建无编译警告或错误。
- 新增 `point_storage` 覆盖实际字节数申请、分配失败、申请后失败释放、重试和重复初始化。
  `communication` 共 11 项，新增两调用者共接口的身份隔离、错误设备回复拒绝、
  超时/发送失败后身份清理及 TR 来源保留。`shared_rs485` 共 7 项，
  覆盖两台从机的独立值、独立周期统计、同线北向缓存读取和下发。
- 单模拟从机的旧夹具继续服务于参数和转发回归，双设备 UART 行为由 `shared_rs485`
  的外部帧夹具验证。该套件启动时清空自己的模拟 Flash；产品没有新增自动擦除行为。
  纯采集、关闭协议适配器、参考接口、参数重启探针和掉电故障注入回归继续通过。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh` 通过；MCUboot 与 IDU/IAPv3
  镜像验签、SGZ 导出成功。IDU Flash 122972 B、RAM 102772 B；
  MCUboot Flash 36568 B、RAM 18096 B。
  当前点值需要 162 B；`nm` 确认静态 `point_words` 符号已删除，目标系统堆缓冲区
  为 308 B（包含管理空间），堆对象等另计。相对此前 121836 B / 102120 B，
  本轮合计 Flash 增加 1136 B、RAM 增加 652 B，不能把申请量减少等同于总 RAM 减少。
- 当前七个持久参数包含原五项及设备 11 的两项，因此旧五参数 NVS schema 不再匹配。
  没有增加自动迁移、自动格式化或北向设备 11 的寄存器映射。
- 修改文档的本地链接、脚本语法及 `git diff --check` 通过。

最终日志保存在工作区 `build/IDU-pre/device-memory/tests.log` 与 `build.log`。
本轮未进行实板双从机、RS485 电气时序、性能或真实 Flash 掉电验证。

## 按参数存储及格式重新初始化（2026-10-21）

- 持久值改为按 `(dev_id, sig_id)` 分别保存，不存默认值和上下限，不再依赖全表签名。
  临时提交日志仅包含新建或变化值；批量中途掉电后补完整批，南向恢复仍只读不重放。
- `parameter_records` 用同一个 Flash 文件启动 11 次独立进程，覆盖新增与重排行、删除后重加、
  不同设备同信号及哈希冲突隔离、无变化 flush 不写 Flash、seed 回调失败不清空、
  范围改变拒绝加载、类型/长度不兼容与旧快照触发重新初始化。
- `points` 按 256 个写入切点验证多参数提交只出现整批旧值或整批新值，覆盖记录历史 CRC 恢复、
  未决南向意图、丢失应答与独立进程重启。`forwarding` 还验证旧意图被清除且不下发，
  自动清空在逐个扇区处失败可重试，并断言所有擦除均在参数分区内。
- `reference_services` 验证单参数长度转换只执行一次、拒绝转换时清空并恢复默认值。
  旧整体快照解析、增量迁移字段和旧 schema 授权均已删除；版本 2 格式不兼容时按用户指定
  自动清空参数区，并记录警告日志及 `DP_PARAM_STORAGE_RESET` 状态。旧历史章节的拒绝格式化策略
  由[当前存储决策](adr/0005-parameter-records.md)替代。
- `ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh` 通过：
  59 次 native 执行、328 次 Ztest 成功，主机 C/Python、IAP 编解码及镜像回归通过；无编译警告。
  当前沙箱不支持 LeakSanitizer，因此关闭泄漏扫描，ASan/UBSan 保持启用。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh` 通过，MCUboot/IDU/IAPv3 镜像验签及 SGZ 导出通过。
  IDU Flash 124548 B、RAM 102772 B；相对上一轮 Flash 增加 1576 B、静态 RAM 不变。
  MCUboot 仍为 Flash 36568 B、RAM 18096 B。

日志位于工作区 `build/IDU-pre/parameter-records/{all-tests.log,product-build.log}`。
本轮未进行实物 NOR 掉电、实际串口或运行时栈余量测试。

## Modbus 采集表直接配置毫秒周期（2026-10-21）

- `MODBUS_RTU_TABLE_S` 的 8 位 `rqst_cyl` 编码改为 `uint16_t period_ms`，应用逐行直接填写毫秒数。
  删除 IDU 周期转换函数、转换注册接口和周期编码宏；`.conf` 的 500 ms 产品周期保持不变。
- 0 继续表示只读点初始化采集、FLAG_COLLECT 下发点仅按指令执行；非零值直接参与轮询。
  UPP 模型保留独立周期策略，与本次 Modbus 表字段变更分开。
- `reference_config` 在未注册 Modbus 周期转换的情况下验证 0、1、500、8000、60000、
  UINT16_MAX，确认不截断并正确区分初始化请求；`table_registry` 验证同设备各行分别使用
  100 ms 和 2000 ms，并保留仅下发及周期反馈回归。
- `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`
  通过：59 次 native 执行、328 次 Ztest 成功，主机及 IAP 回归通过，无编译警告。
- `CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh` 通过，MCUboot／IDU／IAPv3 验签及 SGZ 导出通过。
  IDU Flash 124400 B、RAM 102764 B；相对此前分别减少 148 B 和 8 B。

日志位于工作区 `build/IDU-pre/collect-period/{tests.log,build.log}`；未进行实板时序测量。
