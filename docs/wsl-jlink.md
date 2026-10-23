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
       GD32E505VET7 / IDU
```

## 使用项目脚本

两个入口统一放在应用的 `scripts/` 目录：

- [debug.sh](../scripts/debug.sh)：Linux/WSL 运行 Arm GDB，连接现有 Windows 服务。
- [debug-server.ps1](../scripts/debug-server.ps1)：Windows 启动 J-Link GDB Server，自动加载 Zephyr RTOS 插件。

在应用目录下，先在 Windows PowerShell 启动服务；器件名须按下文的设备选择器核对：

```powershell
# 先预览，再去掉 -DryRun 启动；此处只示例已确认通用映射的情况。
.\scripts\debug-server.ps1 -Device GD32E505VE -DryRun
.\scripts\debug-server.ps1 -Device GD32E505VE
```

Windows 安装目录自动从 PATH、注册表或 Program Files 下定位，也可显式填写：

```powershell
.\scripts\debug-server.ps1 -Device GD32E505VE -JLinkDir 'C:\Program Files\SEGGER\JLink_V878' -Port 2331 -Speed 1000
```

默认允许远程连接，用于 NAT WSL；mirrored 模式可增加 `-LocalhostOnly`。
多只探针可增加 `-SerialNumber`。脚本不改防火墙，按下文限制为当前 WSL 来源。
若执行策略阻止从 WSL 路径运行脚本，可仅对本次进程执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\debug-server.ps1 -Device GD32E505VE
```

再在 Linux/WSL 的应用目录执行（从工作区根目录运行时，在脚本前加 `applications/IDU-pre/`）：

```bash
# NAT：替换为实际 Windows 主机地址。
./scripts/debug.sh --host 172.30.96.1 --dry-run
./scripts/debug.sh --host 172.30.96.1
# mirrored：默认连接 127.0.0.1。
./scripts/debug.sh
```

Linux 默认使用当前 MCUboot + IAPv3 应用构建，读取其 `.config` 识别 ELF 名称，读取 `CMakeCache.txt` 定位本次工具链 GDB；必要时用 PATH 中的 Arm GDB。
相对路径参数以工作区根目录为准，脚本可从任意当前目录调用。例如选择 MCUboot 符号或其他产物：

```bash
./scripts/debug.sh --host 172.30.96.1 --build-dir build/IDU-pre/mcuboot-iapv3/mcuboot
./scripts/debug.sh --host 172.30.96.1 --elf /path/with\ spaces/application.elf --gdb /path/to/arm-none-eabi-gdb
./scripts/debug.sh --help
```

两端端口必须一致。Linux 默认执行 `target remote` 与 `monitor halt`，使用 `-nx` 跳过 GDB 初始化文件，不构建、不下载、不复位；`--` 后可追加 GDB 参数。
Windows 端保留寄存器、不主动复位，但启动后会 halt；长期暂停仍须处理下文的外部看门狗。
脚本已做 Bash 参数/路径验证及 Windows PowerShell 5.1 语法与 DryRun 验证，未连接探针或实板。

## 已核实的本机条件

- 用户确认 MCU 为 **GD32E505VET7**。官方数据手册表 2-1、6-1 对应 512 KB Flash、128 KB SRAM、LQFP100，T7 为 -40…+105°C 温度档。[GD32E505 Rev2.3](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)
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

设备名称需要单独核对：当前 [SEGGER 公开列表](https://www.segger.com/supported-devices/gigadevice/)列出 `GD32E505VET6` 等，未确认精确 `GD32E505VET7`。
本机 V8.78 DLL 的只读字符串检查发现 `GD32E505VE`、`GD32E505VET6`，未发现 `GD32E505VET7`；字符串不能代替运行时设备列表。
先在 Windows **J-Link GDB Server GUI** 的设备选择器中搜索完整型号；若没有，再核对是否提供通用 `GD32E505VE` 的 512 KB 配置。
通用配置只有在当前安装实际接受、容量及连接行为匹配后才使用；若仍不确定，升级 J-Link 或向 SEGGER 确认映射，不选别的芯片系列作为 Flash loader。
厂家手册显示 VET6/VET7 的容量和封装相同、温度档不同，但这不等于 SEGGER 对 VET7 做过独立验证。

下面 `$device` 必须填上述步骤确认的 J-Link 名称，不能把占位符直接执行。

## 2. Windows 启动 GDB Server

Windows PowerShell：

```powershell
$server = 'C:\Program Files\SEGGER\JLink_V878\JLinkGDBServerCL.exe'
$device = '<设备选择器确认的名称>'
$rtos = 'C:\Program Files\SEGGER\JLink_V878\GDBServer\RTOSPlugin_Zephyr.dll'
& $server -device $device -if SWD -endian little -speed 1000 -port 2331 -LocalhostOnly 1 -noir -noreset -halt -rtos $rtos
```

例如实际确认了通用配置后，变量可填 `$device = 'GD32E505VE'`。
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

**不要把未签名应用 ELF 用 GDB `load` 当作默认烧录流程**：它不会替你安装同次构建的 MCUboot 头、签名与启动程序。
应用槽位从 `0x08010000` 开始，头长 512 B；实际向量位于 `0x08010200`。
本项目的导出、签名和地址检查见[构建与升级](build-and-upgrade.md)。

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

本板 PC13 每 10 ms 翻转喂外部看门狗。CPU 被 J-Link halt 后，内核定时器也无法正常执行，不能继续提供边沿。
关闭片内 watchdog 的 debug-freeze 选项不能自动暂停板外芯片。
MCUboot 早期将 PC13 浮空以禁用外狗，应用 `status_init()` 又恢复输出；因此在应用运行后长时间断点可能持续复位。
这来自本项目板级代码和硬件契约，实际禁用电路仍需验证。

推荐按阶段调试：

1. 先在 MCUboot 正常启动路径到达 `main`、`status_init()` 之前验证连接、符号和断点。
2. 调试初始化之后的长期断点前，按原理图确认外狗禁用/隔离办法；有调试跳线则使用该跳线。
3. 若只有 PC13 浮空禁用方式，需另行做可恢复的调试专用停狗机制，明确暂停和恢复顺序，再用于长期断点；当前没有实现，不能直接声称可安全单步任意位置。
4. 恢复业务验证时启用真实外狗，重新上电/复位后验证采集、转发、IAP 与任务超时复位。

不要把 `CONFIG_WATCHDOG_MONITOR=n` 当作现成解决办法：本产品要求该服务，配置检查会失败。
也不要为了断点在故障路径长期代喂，或随意写寄存器后让正式固件一直失去外狗保护。
参见[板级钩子](../boards/idu/idu_pre/board.c)、[任务看门狗](task-watchdog.md)。

## 7. 常见问题

| 现象 | 优先检查 |
| --- | --- |
| 无法连接 TCP 2331 | Windows Server 是否监听；NAT/mirrored 地址是否正确；localhost-only 与窄范围防火墙规则 |
| Server 找不到 J-Link | Windows 驱动、USB 占用；退出 Commander/J-Flash；不要同时将探针 attach 到 WSL |
| Unknown device | 第 1 节设备选择器；VET7 未确认精确名称，不套用其他系列 |
| VTref=0 或无法连接目标 | 目标供电、公共地、SWDIO/SWCLK、NRST、降低 SWD 速度 |
| 一暂停就复位 | 外部看门狗，按第 6 节处理 |
| 跳转地址异常或 MCUboot 拒绝启动 | 用匹配密钥和本次签名 HEX，检查外部镜像交换状态，不用裸 ELF 覆盖 slot0 |
| 源码行/变量不对应 | 板上固件与 ELF 是否同次构建；编译优化与源文件版本 |
| 串口看不到日志 | USART0 PA9/PA10，115200 8N1；USB-UART 接 Windows COM 口即可，与 J-Link TCP 调试独立 |

usbipd 是后备路线：需要 Linux 直接占用探针时才转接，届时在 WSL 安装 Linux J-Link 包；设备转接后 Windows 不能同时使用。
当前推荐路线无需 usbipd、无需 Linux J-Link Server，也不需要改动 Zephyr 上游或增加板级 runner。
