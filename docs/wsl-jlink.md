# Zephyr 在 WSL 中通过 Windows J-Link 远程 GDB 调试

[文档导航](README.md) · [构建与升级](build-and-upgrade.md) · [官方资料核对](wsl-jlink-sources.md)

推荐 Windows 管理 J-Link USB 并运行 **J-Link GDB Server**，WSL 编译并运行 Arm GDB / VS Code 调试扩展。
两端通过 TCP 2331 通信，无需转接 USB。目标板运行 Zephyr 固件，Windows Server 通过 SWD 调试它；无需在 Zephyr 中启用 `CONFIG_GDBSTUB`。这里的服务器是 GDB Server，不是 J-Link Remote Server；后者转发探针协议，是另一套连接方式。
这一组合依据 [SEGGER GDB Server](https://kb.segger.com/J-Link_GDB_Server) 的 TCP 能力与 [Microsoft WSL 网络](https://learn.microsoft.com/en-us/windows/wsl/networking) 工作方式。

```text
VS Code Remote WSL / Arm GDB
              │ TCP :2331
Windows JLinkGDBServerCL.exe
              │ USB
           J-Link
              │ SWD + GND + VTref + 建议 NRST
       GD32E505VET6 / IDU
```

## 直接在 J-Link GDB Server GUI 加载

使用 [gd32e505vet6.JLinkScript](../scripts/gd32e505vet6.JLinkScript)，无需启动 Python 或 PowerShell 脚本。
将文件复制到 Windows 可访问的位置，例如 `C:\Users\81966\Documents\gd32e505vet6.JLinkScript`。

在 Windows **J-Link GDB Server** 的目标配置界面设置：

| 项目 | 设置 |
| --- | --- |
| Device | `GD32E505VET6` |
| Target Interface | `SWD` |
| Speed | `1000 kHz` |
| J-Link Script File | 选择 `gd32e505vet6.JLinkScript` |
| GDB Port | `2331` |
| Localhost only | NAT WSL 取消勾选；mirrored WSL 可勾选 |
| Init regs on start | 关闭，保留寄存器现场 |

加载位置是 **J-Link Script File**，不是 **GDBInit file**。前者使用 C 风格回调，后者是 GDB Server 的远程命令文件，两者语法和用途不同。
本文件的 `ConfigTargetSettings()` 指定 VET6、SWD 与小端模式；TCP 端口和远程访问属于主机服务器设置。
RTOS 插件通过服务器的 `-rtos` 启动参数加载，不能把它当作所有 GUI 版本都提供的输入框，也不能放进 `.JLinkScript`。

不使用 Python 启动脚本时，可以创建 Windows 快捷方式，在“目标”中填写以下整行，之后双击它打开 GUI：

```text
"C:\Program Files\SEGGER\JLink_V878\JLinkGDBServer.exe" -rtos "C:\Program Files\SEGGER\JLink_V878\GDBServer\RTOSPlugin_Zephyr.dll"
```

没有传 `-device`，因此仍可在启动配置对话框中选择 VET6、SWD、速度和 J-Link Script File。
这里打开的是 GUI 版 `JLinkGDBServer.exe`，不是命令行版 `JLinkGDBServerCL.exe`。
插件只负责让 GDB 查看 Zephyr 线程；普通断点、单步、寄存器查看可以先不加载它。
保留默认连接/复位流程，文件不执行目标寄存器读写、下载、擦除或外部看门狗停用。
来源：[SEGGER Script File API](https://kb.segger.com/J-Link_script_files)、[GDB Server 参数与界面](https://kb.segger.com/J-Link_GDB_Server)。

启动 Server，出现等待 GDB 客户端连接的状态后，在 WSL 应用目录运行：

```bash
./scripts/debug.sh --host <Windows主机IPv4>
```

mirrored 模式可直接 `./scripts/debug.sh`。查看 Zephyr 线程仍需用下文的 `CONFIG_DEBUG_THREAD_INFO=y` 构建对应固件；断点与外部看门狗限制也仍适用。
文件已按官方回调和受限 C 语法核对；未进行 J-Link 实际加载或实板连接验证。

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
关闭看门狗，但不自动打开线程调试元数据。

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

Linux 默认使用当前 MCUboot + IAPv3 应用构建，读取其 `.config` 识别 ELF 名称，默认下载同目录同名的 `.signed.hex`，读取 `CMakeCache.txt` 定位本次工具链 GDB；必要时用 PATH 中的 Arm GDB。
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
当前原生 `.signed.hex` 已转换为 BIN 并通过官方 imgtool 公钥验签，应用内容与同次构建的原始 BIN 一致。
默认签名 HEX、显式导出 HEX、ELF `main` 符号保留、`--attach` 无写入及下载失败停止后续复位均已通过真实 Arm GDB＋本地模拟服务验证；日志位于工作区 `build/IDU-pre/debug-signed-rsp-check.log`。
先前裸 ELF 下载的日志仅保留为历史记录；本轮未连接实际探针或烧录实板。

## 已核实的本机条件

- 按用户最新说明，目标 MCU 修正为 **GD32E505VET6**。官方数据手册表 2-1、6-1 对应 512 KB Flash、128 KB SRAM、LQFP100，T6 为 -40…+85°C 温度档。[GD32E505 Rev2.3](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)
- 当前为 WSL2，Windows 已安装 `C:\Program Files\SEGGER\JLink_V878`。
- WSL 已有可运行的 GDB：`/home/ryan/Tools/toolchain/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb`。
- `/etc/wsl.conf` 的 `appendWindowsPath=false`，因此 WSL 中找不到 `powershell.exe` 的短命令并不代表 Windows 没有安装它；下面 Windows 命令在 Windows PowerShell 执行。
- 应用当前 ELF 已包含 `.debug_info`、`.debug_line`，可使用现有构建进行源码调试；当前采用尺寸优化，部分变量可能被优化掉。
- 尚未运行探针连接、烧录、Windows 防火墙变更或实板断点验证；没有把文件存在性当作硬件连接成功。

## Zephyr 专用的构建与线程调试

先在工作区根目录创建仅本地使用的调试配置，再通过现有构建脚本装配：

```bash
mkdir -p build
cat > build/jlink-debug.conf <<'CONF'
CONFIG_DEBUG_OPTIMIZATIONS=y
CONFIG_DEBUG_THREAD_INFO=y
CONF
./applications/IDU-pre/scripts/build.sh --extra-conf build/jlink-debug.conf
```

当前 Zephyr 4.4.2 中 `DEBUG_THREAD_INFO` 自动选择 `THREAD_MONITOR` 和 `THREAD_NAME`，无需重复填写。
`-Og` 改善单步体验，线程元数据用于 debugger RTOS awareness；编译选项及元数据会改变镜像，须烧录本次签名产物并使用本次 ELF。
本方案仅提供操作步骤，本轮未执行这个调试配置构建。

Windows GDB Server 必须同时加载本机已安装的 Zephyr RTOS 插件：

```text
C:\Program Files\SEGGER\JLink_V878\GDBServer\RTOSPlugin_Zephyr.dll
```

WSL 的 GDB 此时可用 `info threads`、`thread <编号>`、`thread apply all bt` 检查 Zephyr 线程与调用栈。
内核初始化之前线程列表可能不完整，先运行到应用 `main` 再检查。插件提供调试线程视图，不代表非侵入实时追踪，也不自动给线程创建业务名称。
来源：[Zephyr RTOS 插件](https://github.com/zephyrproject-rtos/jlink-zephyr)、本地 `zephyr/subsys/debug/Kconfig` 和 `zephyr/kernel/Kconfig`。

`CONFIG_GDBSTUB` 是另一种把 RSP 服务放在目标固件中的路径，当前官方实现使用串口；本方案的 TCP server 在 Windows，选项无需启用。
见 [Zephyr GDB stub](https://docs.zephyrproject.org/latest/services/debugging/gdbstub.html)。

## 1. 连接硬件与选择设备

按板上调试座定义连接 SWDIO → PA13、SWCLK → PA14、GND，以及目标电压参考 VTref；建议连接 NRST。
板子正常供电，VTref 接板上目标 I/O 电压参考，不能把它当成任意供电输出。
不要根据网上另一种插座的编号接线，板上排针针序仍需对照实物。芯片复用说明见[官方手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)，探针针脚见 [SEGGER SWD](https://kb.segger.com/SWD)。

本项目早期钩子释放 JTAG、保留 SWD；请选 **SWD**，初始速度建议 **1000 kHz**。连接不稳定时降至 100–400 kHz。

按用户最新修正，目标及 J-Link 默认设备名统一为 **GD32E505VET6**。
用户实板日志中，探针已成功连接，随后报 `Failed to get index for device name 'GD32E505VET7'`；这是设备库名称查找失败，尚不能据此判断板端供电或 SWD 连线。

本机 Windows Python 使用 `JLINKARM_DEVICE_GetIndex()` 只读查询 V8.78 DLL，结果为：

| 名称 | 设备索引 |
| --- | ---: |
| GD32E505VET7 | -1（不识别） |
| GD32E505VE | 974 |
| GD32E505VET6 | 974 |

相同索引说明两个受支持名称指向同一库项。官方数据手册中 VET6/VET7 同为 E505、512 KB Flash、128 KB SRAM、LQFP100，温度档不同；根据最新型号说明直接采用 VET6 完整配置。
来源：[厂家数据手册](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)、[SEGGER 支持表](https://www.segger.com/supported-devices/gigadevice/)及上述本机索引查询。

这验证了设备名可识别，尚未验证该配置连接实板成功。下面 `$device` 在本机 V8.78 填 `GD32E505VET6`；其他目标或库版本按设备选择器与容量核对。

## 2. Windows 启动 GDB Server

Windows PowerShell：

```powershell
$server = 'C:\Program Files\SEGGER\JLink_V878\JLinkGDBServerCL.exe'
$device = 'GD32E505VET6'
$rtos = 'C:\Program Files\SEGGER\JLink_V878\GDBServer\RTOSPlugin_Zephyr.dll'
& $server -device $device -if SWD -endian little -speed 1000 -port 2331 -LocalhostOnly 1 -noir -noreset -halt -rtos $rtos
```

此名称已通过当前 V8.78 设备索引查询。
仅一只探针时无需选择序号；多只时增加 `-USB <探针序列号>`。GUI 中也可选探针、设备和 SWD，再等待连接。
`-noir -noreset` 用于保留运行现场，服务器启动仍会 halt；这一步本身也可能因外部看门狗而复位，参见第 6 节。
参数含义见 [SEGGER 参数说明](https://kb.segger.com/J-Link_GDB_Server#Command_line_options)。

网络分两种，按当前设置选一种即可：

| WSL 网络 | WSL 中连接地址 | GDB Server 参数 |
| --- | --- | --- |
| mirrored，支持 Windows localhost 互访 | `127.0.0.1:2331` | `-LocalhostOnly 1` |
| 默认 NAT | Windows 在 WSL 虚拟网络的主机地址 | 将上面的参数改成 `-LocalhostOnly 0` |

NAT 模式在 **WSL 普通终端**查询 Windows 地址：

```bash
ip route show default | awk '/default/ {print $3; exit}'
```

不要把 DNS tunneling 下 `/etc/resolv.conf` 的 nameserver 当作 Windows 主机地址。
若 NAT 被 Windows 防火墙拦截，在管理员 PowerShell 为 **GDB Server 程序、TCP 2331、当前 WSL IPv4 来源**建立窄范围入站规则：

```powershell
$server = 'C:\Program Files\SEGGER\JLink_V878\JLinkGDBServerCL.exe'
$wslAddress = '<WSL中hostname -I显示的IPv4地址>'
New-NetFirewallRule -DisplayName 'WSL J-Link GDB 2331' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 2331 -Program $server -RemoteAddress $wslAddress -Profile Any
```

WSL 地址变化后更新该规则；无需开放 2332/2333、关闭整个防火墙或设置公网端口转发。
mirrored 的启用条件、`.wslconfig` 和 NAT 的区别见 [Microsoft 官方说明](https://learn.microsoft.com/en-us/windows/wsl/networking)。不必为了此方案强制修改网络模式。

## 3. WSL 构建、产物与首次安装

```bash
cd /home/ryan/Documents/zephyrproject
./applications/IDU-pre/scripts/build.sh
```

| 文件 | 用途 |
| --- | --- |
| `build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-factory.hex` | MCUboot + 签名应用，首次安装新布局 |
| `build/hex/IDU-pre-mcuboot-iapv3/IDU-pre.hex` | 已有匹配 MCUboot 时，写入签名应用 |
| `build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/IDU-pre.elf` | 应用符号与源码调试 |
| `build/IDU-pre/mcuboot-iapv3/mcuboot/zephyr/zephyr.elf` | MCUboot 符号与源码调试 |

**完整安装使用签名 factory HEX**。`debug.sh` 仅下载签名应用 HEX，保留板上现有 MCUboot，ELF 用于源码符号；独立 `flash.sh` 用于完整安装启动程序及应用。
应用槽位从 `0x08010000` 开始，头长 512 B；实际向量位于 `0x08010200`。
本项目的导出、签名和地址检查见[构建与升级](build-and-upgrade.md)。

### Linux 独立烧录入口

保留 Windows J-Link GDB Server 运行，先退出命令行 GDB / VS Code 调试会话，释放客户端连接。
在 Linux/WSL 应用目录执行：

```bash
./scripts/flash.sh --host 172.30.96.1 --dry-run
./scripts/flash.sh --host 172.30.96.1
```

把示例地址替换为 Windows 主机地址；mirrored 模式可直接 `./scripts/flash.sh`。
默认读取本地 `build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-factory.hex`，通过 GDB 传输，不需要复制 HEX 到 Windows，也不需要应用 ELF。
该入口单独完成烧录，不调用构建脚本、不打开交互调试界面、不启动或关闭 Windows Server。

这是**完整安装/重装**入口：先检查 HEX 记录校验、内部 Flash 地址范围、MCUboot 头和启动/应用向量，然后连接目标，复位并擦除 MCU 内部 Flash，下载合并 HEX，最后复位运行并退出。
完整擦除同时清除内部 slot0 的旧 trailer；应用单独的 `IDU-pre.hex` 会因缺少 MCUboot 数据而被预检拒绝。
结构预检不替代密码学验签，签名由既有 build/export 流程验证。脚本开启 J-Link 的下载校验，使用 GDB 命令文件使任一命令报错后停止后续步骤。
命令依据：[GDB load](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Target-Commands.html)、[GDB 命令文件错误处理](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Command-Files.html)、[J-Link monitor 命令](https://kb.segger.com/J-Link_GDB_Server)。

其他选项：

```bash
./scripts/flash.sh --host 172.30.96.1 --port 2331
./scripts/flash.sh --host 172.30.96.1 --hex build/hex/IDU-pre-mcuboot-smp/IDU-pre-factory.hex
./scripts/flash.sh --help
```

它不清理外部 SPI NOR；首次从旧布局迁移时，仍按下文处理外部 slot1/scratch。实际板上的外部看门狗与擦写过程仍需验证。
本轮只执行离线预检、DryRun 和模拟 GDB 服务验证，不对实板执行烧录。
模拟验证使用真实 Arm GDB，核对写入字节、地址、旧 trailer 擦除及写入失败后不发送复位/运行命令；日志在工作区 `build/IDU-pre/flash-rsp-check.log`。

### Windows Commander 手工安装（可选）

首次烧录推荐先退出 GDB Server，再用 Windows J-Link Commander，避免两个程序同时占用探针。
把同次构建的 factory HEX 复制到 Windows 的 `C:\Temp\IDU-pre-factory.hex`，例如在 WSL 执行：

```bash
mkdir -p /mnt/c/Temp
cp build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-factory.hex /mnt/c/Temp/
```

Windows PowerShell 运行 Commander（`$device` 同第 1 节）：

```powershell
& 'C:\Program Files\SEGGER\JLink_V878\JLink.exe' -device $device -if SWD -speed 1000 -autoconnect 1
```

Commander 交互命令：

```text
r
h
loadfile C:\Temp\IDU-pre-factory.hex
r
g
exit
```

`loadfile` 按 HEX 记录地址写入，并默认验证，不需要额外给 HEX 添加基址；具体语义见 [J-Link Commander](https://kb.segger.com/J-Link_Commander#LoadFile)。
这些是供操作者执行的实际写 Flash 步骤，本轮没有执行。

首次从旧启动布局迁移还需要清理外部 NOR 新 slot1/scratch：`0x300000..0x371fff`。
**普通内部 Flash loader 不会清理 SPI 外部 W25Q32**；须使用已验证的板级维护程序或外部编程器，保留 NVS 和 IAP 参数区。
当前仓库没有可直接套用的 J-Link 外部 NOR 擦除脚本，不把 `erase` 全片命令当作这个操作。已有正确 MCUboot 布局的正常升级遵循其交换状态，不随意清理回退镜像。

## 4. WSL 命令行 GDB 先连通

重新启动 Windows GDB Server 后，在 WSL：

```bash
cd /home/ryan/Documents/zephyrproject
/home/ryan/Tools/toolchain/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb \
  build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/IDU-pre.elf
```

GDB 内，mirrored 用 `127.0.0.1`；NAT 将它改为第 2 节查到的 Windows 地址：

```gdb
target remote 127.0.0.1:2331
monitor halt
info registers
bt
info threads
thread apply all bt
```

已安装同次固件、已处理第 6 节看门狗限制后，从复位检查应用入口：

```gdb
monitor reset
hbreak main
continue
```

这里 ELF 只用于符号，没有 `load`。调试 MCUboot 时改用其 ELF，入口/断点也选其代码。
通过 `monitor reset` 从 MCUboot 正常启动；不手工将 PC 跳到应用来绕过启动校验和硬件交接。

## 5. VS Code Remote WSL

在 WSL 窗口安装 **Cortex-Debug**（`marus25.cortex-debug`），确保扩展实际安装在 WSL 端。
本机已能使用 clangd；clangd 负责代码导航，Cortex-Debug 与 Arm GDB 负责断点调试。
Remote WSL 的安装位置规则见 [VS Code 官方文档](https://code.visualstudio.com/docs/remote/wsl)。

下面模板以打开 `/home/ryan/Documents/zephyrproject` 为工作区根目录，保存到该窗口使用的 `.vscode/launch.json`；
如果已有配置，只合并 configuration，不覆盖原内容。根目录配置文件属于本机工作区，本轮没有修改它。

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "IDU · Windows J-Link GDB Server",
      "type": "cortex-debug",
      "request": "attach",
      "servertype": "external",
      "cwd": "${workspaceFolder}",
      "executable": "${workspaceFolder}/build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/IDU-pre.elf",
      "gdbPath": "/home/ryan/Tools/toolchain/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb",
      "gdbTarget": "${input:jlinkGdbTarget}",
      "overrideAttachCommands": ["monitor halt"],
      "overrideResetCommands": ["monitor halt", "monitor reset"]
    }
  ],
  "inputs": [
    {
      "id": "jlinkGdbTarget",
      "type": "promptString",
      "description": "Windows GDB Server 地址，NAT 模式填写 Windows 主机 IPv4:2331",
      "default": "127.0.0.1:2331"
    }
  ]
}
```

按 F5 后输入连接地址。`attach` 连接现有固件并停止，不自动构建或下载；不能同时让命令行 GDB 占用同一个会话。
重置时显式覆盖外部 server 默认的 OpenOCD 命令；不要在 `overrideAttachCommands` 重复 `target remote`，扩展已处理连接。
来源：[Cortex-Debug external 说明](https://github.com/Marus/cortex-debug/wiki/External-gdb-server-configuration)、[官方配置字段](https://github.com/Marus/cortex-debug/blob/master/debug_attributes.md)。

## Zephyr west attach / debug 的接入条件

当前自定义板没有 `board.cmake`，应用构建未生成 `runners.yaml`；因此不能把 `west attach -r jlink` 描述成当前直接可用的命令。
上面的 SDK GDB 连接本次 Zephyr ELF 已能完成远程调试，不依赖 runner。

若后续统一采用 west，需要在应用仓库内为板增加 `boards/idu/idu_pre/board.cmake` 并重新构建，例如：

```cmake
board_runner_args(jlink "--iface=swd" "--speed=1000")
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
```

再使用本次应用 domain 的构建目录，显式填写经确认的器件名和 Windows 主机地址：

```bash
source .venv/bin/activate
west attach -d build/IDU-pre/mcuboot-iapv3/IDU-pre -r jlink \
  --device '<已确认的J-Link器件名>' --gdb-host '<Windows主机IPv4>' --gdb-port 2331
```

这些 runner 接入修改本轮没有实施，命令只在上述前置条件完成后适用。
本地 `zephyr/scripts/west_commands/runners/jlink.py` 还有一个实际限制：即使设置 `--gdb-host`，仍先检查 Linux J-Link Commander/共享库和 `pylink`，用来确定版本。
所以仅安装 Windows J-Link 时优先使用直接 SDK GDB；采用 west runner 还需补齐这些 WSL 依赖，USB 仍可留在 Windows。

当前 MCUboot 签名流程优先选 **attach**，不要默认运行 `west debug`：当前 J-Link runner 的 debug 路径会 `monitor reset` 后 `load` ELF，可能破坏签名镜像内容。
`west debugserver --gdb-host ...` 也不是远程连接命令，当前 runner 会拒绝该组合；Windows Server 已经单独运行，客户端选 attach。
依据：[Zephyr west 文档](https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html)及本地 `runners/jlink.py`、`run_common.py`。

## 6. 本板断点与外部看门狗

常规 `prj.conf` 默认启用看门狗。单步调试时从应用目录执行
`./scripts/build.sh --extra-conf "$PWD/conf/debug.conf"`，显式设置
`CONFIG_WATCHDOG_SOFTWARE=n` 与 `CONFIG_WATCHDOG_EXTERNAL=n`：应用跳过任务监控和外部喂狗初始化；
调用点明确请求的 RESET 仍会复位，调试配置不改写故障动作。
PC13 保持 MCUboot 早期设置的浮空状态，不恢复输出。需重新构建、下载并从
MCUboot 复位启动后生效；此配置不会主动禁用旧固件已经启用的看门狗。
调试结束后不带该额外配置重新构建、下载，以恢复看门狗保护。
以下限制适用于恢复看门狗初始化后的固件。

本板 PC13 每 10 ms 翻转喂外部看门狗。CPU 被 J-Link halt 后，内核定时器也无法正常执行，不能继续提供边沿。
关闭片内 watchdog 的 debug-freeze 选项不能自动暂停板外芯片。
MCUboot 早期将 PC13 浮空以禁用外狗，应用 status_init 显式调用 watchdog 模块后恢复输出；因此在应用运行后长时间断点可能持续复位。
这来自本项目板级代码和硬件契约，实际禁用电路仍需验证。

推荐按阶段调试：

1. 先在 MCUboot 正常启动路径到达 main 或 status_init 中启动 watchdog 之前验证连接、符号和断点。
2. 调试初始化之后的长期断点前，按原理图确认外狗禁用/隔离办法；有调试跳线则使用该跳线。
3. 若只有 PC13 浮空禁用方式，需另行做可恢复的调试专用停狗机制，明确暂停和恢复顺序，再用于长期断点；当前没有实现，不能直接声称可安全单步任意位置。
4. 恢复业务验证时启用真实外狗，重新上电/复位后验证采集、转发、IAP 与任务超时复位。

旧的 `CONFIG_WATCHDOG_MONITOR` 已移除；当前两个开关独立控制，产品允许关闭。
也不要为了断点在故障路径长期代喂，或随意写寄存器后让正式固件一直失去外狗保护。
参见[板级钩子](../boards/idu/idu_pre/board.c)、[任务看门狗](task-watchdog.md)。

### 栈故障现场

`arch_system_halt(reason=2)` 表示 `K_ERR_STACK_CHK_FAIL`。先保留现场，使用
`bt`、`p _kernel.cpus[0].current->stack_info` 和 `info registers msp psp`
确认线程及栈边界；需要重现时在 `z_arm_fatal_error` 设置硬件断点并读取 `*esf`。
UsageFault 处理函数会清除 UFSR，因此在最终停机循环读到 CFSR 为零，不能排除已处理的栈异常。

本次实板回溯定位到采集 RAW `send()` 的栈申请：可写栈起点 `0x2000f4e0`，
故障时 PSP 为 `0x2000f5a8`，距下界只剩 200 B；该函数入口随后申请 1140 B。
当时 Arm 编译配置的 `-fstack-usage` 报告中，调用链至 RAW `send()` 已累计约
4992 B，尚未包含后续 vport/串口发送及额外运行余量，曾将采集栈临时提高到 8192 B。
随后[工作区迁移](memory-management.md)将大缓冲改用 `k_malloc()`，采集栈恢复为
4096 B，保留硬件栈保护和 1100 B 协议能力。重新下载后仍需验证持续通信及运行余量。

## 7. 常见问题

| 现象 | 优先检查 |
| --- | --- |
| 无法连接 TCP 2331 | Windows Server 是否监听；NAT/mirrored 地址是否正确；localhost-only 与窄范围防火墙规则 |
| Server 找不到 J-Link | Windows 驱动、USB 占用；退出 Commander/J-Flash；不要同时将探针 attach 到 WSL |
| Unknown device / Failed to get index | 当前 V8.78 使用 `GD32E505VET6`；更新 Windows 上复制的脚本或传 `--device GD32E505VET6` |
| VTref=0 或无法连接目标 | 目标供电、公共地、SWDIO/SWCLK、NRST、降低 SWD 速度 |
| 一暂停就复位 | 外部看门狗，按第 6 节处理 |
| 跳转地址异常或 MCUboot 拒绝启动 | 用匹配密钥和本次签名 HEX，检查外部镜像交换状态，不用裸 ELF 覆盖 slot0 |
| 源码行/变量不对应 | 板上固件与 ELF 是否同次构建；编译优化与源文件版本 |
| 所有寄存器是同一异常值，CPUID 也读失败 | 先保存 Server 日志并重建调试会话；不能据这些值判断栈损坏，也不要继续下载 |
| 串口看不到日志 | USART0 PA9/PA10，115200 8N1；USB-UART 接 Windows COM 口即可，与 J-Link TCP 调试独立 |

usbipd 是后备路线：需要 Linux 直接占用探针时才转接，届时在 WSL 安装 Linux J-Link 包；设备转接后 Windows 不能同时使用。
当前推荐路线无需 usbipd、无需 Linux J-Link Server，也不需要改动 Zephyr 上游或增加板级 runner。

### 寄存器异常时的最小诊断

`Ctrl+C` 后停在 `arch_cpu_idle()` 的 `SIGTRAP` 本身不是程序故障证据。
若 PC、SP、LR、xPSR 同时变成相同异常值，在当前 GDB 会话依次执行：

```gdb
monitor halt
maintenance flush register-cache
monitor regs
x/wx 0xE000ED00
x/wx 0xE000EDF0
info registers pc lr sp xpsr
```

`x/wx` 分别读取 CPUID 和 DHCSR。若使用 J-Link 的 `monitor memU32`，只写地址；
不要追加数量参数，第二个数值会被解释成写入数据。命令语法见
[SEGGER GDB Server](https://kb.segger.com/J-Link_GDB_Server#memU32)。
长期暂停前仍须处理第 6 节的外部看门狗。

CPUID 可读、`monitor regs` 正常而 GDB 显示异常时，先刷新寄存器缓存；
CPUID 也无法访问时，刷新缓存不能恢复底层连接。退出当前 GDB，重新启动 Windows
J-Link GDB Server，再用 `./scripts/debug.sh --attach` 验证目标访问。
重连恢复不等于已经解决复发原因；保存 Server 日志，分别验证速度、连接和复位条件。

访问恢复后仍需核对板上应用与当前 ELF。`compare-sections -r` 可辅助比对代码与常量；
签名过程会修改 MCUboot 头，不能仅因包含头部的 `rom_start` 不同就判定整个应用不匹配。
代码段 `text` 也不一致时，不据源码行和局部变量继续判断程序故障；重新下载匹配的签名镜像，
或使用匹配板上镜像的 ELF。仅有 `Transfer rate` / `Resetting target` 文本不是独立校验结果。

脚本回归：先构建 Arm 应用，再运行 `python3 tests/test_debug_session.py -v`。
测试使用真实 SDK GDB 与本机模拟远程目标，不接触探针，覆盖缓存刷新和读取失败时禁止下载；
缺少 SDK GDB 或应用 ELF 时明确跳过。实板诊断记录见
[2026-10-24 验证记录](../.scratch/gdb-access/validation.md)。
