# WSL / Windows J-Link 调试

[文档导航](README.md) · [构建与升级](build-and-upgrade.md)

Windows 运行 J-Link GDB Server，WSL 运行 Arm GDB。板上 MCU 为 GD32E505VET6；
SWDIO=PA13、SWCLK=PA14，连接 GND、VTref，建议连接 NRST。探针与软件操作须按实际接线执行。

以下调试示例从应用仓库目录运行；工作区根目录执行时加 applications/IDU-pre 前缀。

## 使用项目脚本

两个入口统一放在应用的 `scripts/` 目录：

- [debug.sh](../scripts/debug.sh)：Linux/WSL 运行 Arm GDB，连接现有 Windows 服务，默认下载签名应用 HEX、重新载入 ELF 符号并复位后进入调试；`--attach` 只连接。
- [debug-server.py](../scripts/debug-server.py)：Windows Python 启动 J-Link GDB Server，自动加载 Zephyr RTOS 插件。

在应用目录下，先在 Windows PowerShell 或命令提示符中执行：

```powershell
py .\scripts\debug-server.py
```

只做核心断点、寄存器和源码调试，或固件没有 `CONFIG_DEBUG_THREAD_INFO=y` 时使用：

```powershell
py .\scripts\debug-server.py --no-rtos
```

它仍启动 `JLinkGDBServerCL.exe`，省略 RTOS 插件；当前调试用的 `conf/debug.conf`
只关闭外部喂狗，但不自动打开线程调试元数据。

按用户最新说明，目标型号及脚本默认调试器件名均为 `GD32E505VET6`，端口 2331、SWD 1000 kHz，自动查找 J-Link。无需交互输入；默认值集中在脚本顶部，可手动修改。
当前 V8.78 设备库已识别此完整名称，可用 `--device` 显式覆盖；脚本不自动重试其他设备名称。
直接指定并预览参数：

```powershell
py .\scripts\debug-server.py --device GD32E505VET6 --dry-run
py .\scripts\debug-server.py --device GD32E505VET6
```

Windows 安装目录自动从 PATH、注册表或 Program Files 下定位，也可显式填写：

```powershell
py .\scripts\debug-server.py --device GD32E505VET6 --jlink-dir 'C:\Program Files\SEGGER\JLink_V878' --port 2331 --speed 1000
```

默认允许远程连接，用于 NAT WSL；mirrored 模式可增加 `--localhost-only`。
多只探针可增加 `--serial`，帮助使用 `--help`。脚本不改防火墙，按下文限制为当前 WSL 来源。
需要 Windows Python；若没有 `py` 启动器，可用 Windows 的 `python` 命令替代，无需 PowerShell 执行策略设置。
WSL Python 只允许 `--dry-run` 检查参数，实际服务必须由 Windows Python 启动。

再在 Linux/WSL 的应用目录执行（从工作区根目录运行时，在脚本前加 `applications/IDU-pre/`）：

```bash
# NAT：替换为实际 Windows 主机地址。
./scripts/debug.sh --host 172.30.96.1 --dry-run
./scripts/debug.sh --host 172.30.96.1
# mirrored：默认连接 127.0.0.1。
./scripts/debug.sh
```

Linux 默认使用当前 MCUboot + IAPv3 应用构建，读取其 `.config` 识别 ELF 名称，默认下载同目录同名的 `.signed.hex`，读取 `CMakeCache.txt` 定位当前工具链 GDB；必要时用 PATH 中的 Arm GDB。
相对路径参数以工作区根目录为准，脚本可从任意当前目录调用。例如选择 MCUboot 符号或其他产物：

```bash
./scripts/debug.sh --host 172.30.96.1 --build-dir build/IDU-pre/mcuboot-iapv3/mcuboot --attach
./scripts/debug.sh --host 172.30.96.1 --elf /path/with\ spaces/application.elf --hex /path/with\ spaces/application.signed.hex --gdb /path/to/arm-none-eabi-gdb
./scripts/debug.sh --host 172.30.96.1 --attach
./scripts/debug.sh --help
```

两端端口必须一致。Linux 默认连接并 halt，复位后下载签名应用 HEX，重新执行 `symbol-file ELF` 保留源码符号，再次复位使 CPU 从 MCUboot 启动入口开始，停留在 GDB 中等待操作。
不自动构建或运行。`--attach` 只连接、halt，不下载、不复位。使用 `-nx` 跳过 GDB 初始化文件，`--` 后可追加 GDB 参数。
初始化命令放在独立 GDB 命令文件中，任何一步报错都会停止其后续步骤。
脚本在连接、暂停后先读取 CPUID（`0xE000ED00`）；读取失败或返回全零／全一时，
停止启动流程，不继续复位或下载。这个检查证明当时可访问目标，不等同于镜像验签。
`monitor halt/reset` 不会自动刷新 GDB 的寄存器缓存；脚本在暂停和最终复位后执行
`maintenance flush register-cache`，防止把连接时的无效寄存器沿用到后续调试。

默认文件为：

```text
符号：build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/IDU-pre.elf
下载：build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/IDU-pre.signed.hex
```

也可显式指定导出工具生成的签名应用 HEX：

```bash
./scripts/debug.sh --host 172.30.96.1 --hex build/hex/IDU-pre-mcuboot-iapv3/IDU-pre.hex
```

默认应用 HEX 从 `0x08010000` 开始，包含 MCUboot 头、应用和签名；ELF 不参与下载。因此不需要移除 MCUboot，也不再以裸 ELF 改写签名镜像。
板上须已有与该镜像密钥匹配的 MCUboot，HEX 和 ELF 必须来自同次构建；当前脚本不做密码学验签，签名由既有构建/导出流程验证。
第一次安装使用 `flash.sh` 写入合并镜像。debug 不执行全片擦除，不主动清理既有的升级/回退状态；尚有未完成交换时，单靠换用签名 HEX 不能保证启动成功。

下载后可在 GDB 输入：

```gdb
hbreak main
continue
```

硬件断点避免在 MCUboot 校验前用软件断点改写镜像。Windows 端启动后可能 halt；长期暂停仍须处理下文的外部看门狗。

## 首次安装

板上尚无匹配 MCUboot 时，先用 flash.sh 安装 factory 镜像，再使用 debug.sh。
flash.sh 完整擦除内部 Flash；debug.sh 只下载签名应用，不执行全片擦除。
签名 HEX 和 ELF 必须来自同次构建，不用裸 ELF 覆盖签名应用。

## 网络与线程调试

WSL mirrored 模式通常连接 127.0.0.1；NAT 使用实际 Windows 主机地址。
先用 --dry-run 检查命令。两端端口一致，默认 2331；防火墙仅放行需要的程序、端口和 WSL 来源。

需要 RTOS 线程视图时，固件启用 CONFIG_DEBUG_THREAD_INFO=y，Windows 服务加载
Zephyr RTOS 插件；不需要线程视图则使用 --no-rtos。conf/debug.conf 不自动打开线程调试元数据。
当前板未配置 west J-Link runner，直接使用项目脚本。

## 外部看门狗

调试配置仅设置 CONFIG_WATCHDOG_EXTERNAL=n，软件监控和显式 RESET 仍有效。
长期断点会使 CPU 和 k_timer 停止，不能依赖定时器继续喂外部看门狗。
MCUboot 早期将 PC13 浮空，调试固件需从 MCUboot 复位启动后才沿用这一状态。
实际硬件是否因此禁用外狗须核对电路；软件监控暂停接口见[watchdog 模块](../modules/func/watchdog/README.md)。

## 常用诊断

```gdb
monitor halt
maintenance flush register-cache
monitor regs
x/wx 0xE000ED00
x/wx 0xE000EDF0
info registers pc lr sp xpsr
bt
```

CPUID 无法读取时先排查供电、SWD、设备名称和 Server，不继续下载。
CPUID 正常而寄存器异常时刷新缓存并核对镜像与 ELF；日志显示下载完成不等于签名或实板行为验证。
栈异常先保存回溯、线程和 SP，再检查栈边界，避免直接增大栈掩盖其他问题。

脚本回归入口为 tests/test_debug_session.py，使用模拟远程目标，不代表实板连通。
当前结果及待验范围见[验证说明](testing.md)。

参考：[SEGGER GDB Server](https://kb.segger.com/J-Link_GDB_Server)、
[Zephyr RTOS 插件](https://github.com/zephyrproject-rtos/jlink-zephyr)。
