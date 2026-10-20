# 测试、验证与资源记录

[文档导航](README.md) · [构建与升级](build-and-upgrade.md)

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
| `test_registers.c`、`test_boot_image.py`、`test_import_aidc.py` | 转换/寄存器回归、镜像边界、导入工具 |
| `points`、`aidc`、`mvio` | 产品点表、NVS/事务、MV-IO 报文和旧参数迁移 |
| `pipeline`、`models` | 无存储数据流、模型复用、多设备及多总线隔离 |
| `dataprocess`、`collector`、`transmit` | 核心模块独立配置、采集调度、转发模式和恢复 |
| `reference_config`、`reference_dp`、`reference_ports`、`reference_services` | 原表注册、同步字节接口、CO/TR 适配、策略回调和迁移 |
| `boot`、`iap`、`test_iap_codec.py` | 健康确认、IAP 收包/签名拒绝、SGZ/QuickLZ 边界 |

`tests/run.sh` 同时验证 Process 关闭、参数/后端可选配置，并用独立 Flash 临时文件
在新进程中执行参数、MV-IO 和 IAP 恢复阶段。普通运行跳过的 reboot probe 会在
相应阶段单独运行；按用例筛选运行时的其他 SKIP 不表示它们缺少全量覆盖。

`tests/legacy_registers.c` 是早期 Zephyr 映射回归依据，没有执行旧固件。
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

核对 20 MHz 有源时钟及 PLL/总线分频、旧 BOOT/MCUboot 跳转、PC13 看门狗交接、
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
