# WSL 与 J-Link 官方资料核对

供[操作方案](wsl-jlink.md)引用；核对日期：2026-10-23。这里只记录资料与推导，未安装软件、改变 Windows 配置或烧录实板。
最新型号按用户后续说明修正为 GD32E505VET6；下方 VET7 相关段落保留原始输入与排查过程。

## 连接路径

J-Link GDB Server 可运行在 Windows，以 TCP GDB 协议连接 Linux GDB。因此可让 Windows 管理 USB 探针，WSL 只运行构建和 GDB；这是根据产品能力组合出的方案。[SEGGER GDB Server](https://kb.segger.com/J-Link_GDB_Server)

WSL2 默认 NAT 模式下，Linux 访问 Windows 服务使用主机 IP；微软给出的查询方式为 `ip route show | grep -i default | awk '{ print $3}'`。mirrored 模式可用 IPv4 `127.0.0.1` 访问 Windows 服务；启用该模式的系统要求见原文。[Microsoft WSL 网络](https://learn.microsoft.com/en-us/windows/wsl/networking)

SEGGER 旧 WSL 页面建议 Remote Server，并仍写 USB 支持不完整；不能据此认定现代 WSL2 无法转接 USB。微软当前说明提供 usbipd-win 方案：管理员 `usbipd bind --busid <BUSID>`，随后 `usbipd attach --wsl --busid <BUSID>`；WSL 用 `lsusb` 检查。设备转接期间 Windows 无法使用它，`usbipd detach --busid <BUSID>` 可交回。[SEGGER WSL](https://kb.segger.com/WSL)、[Microsoft USB 转接](https://learn.microsoft.com/en-us/windows/wsl/connect-usb)

## GDB Server 参数

命令行需明确设备、接口、端序和速度。速度为 kHz；`-LocalhostOnly 1` 仅接收本机连接，Windows 默认如此；`0` 接受其他地址。NAT 路径若使用 `0`，仅为所需端口和 WSL 来源放行，避免宽泛入站规则。`-USB <序列号>` 从 V8.24 提供；不要把 GDB Server IP 与探针 `-IP` 参数混淆。[SEGGER 参数说明](https://kb.segger.com/J-Link_GDB_Server#Command_line_options)

示例，设备名和路径必须按安装与板上丝印替换：

```powershell
& 'C:\实际安装目录\JLinkGDBServerCL.exe' -device '<已在安装包确认的设备配置名>' -if SWD -endian little -speed 1000 -port 2331 -LocalhostOnly 1
```

用户已确认实板为 **GD32E505VET7**。GigaDevice 官方数据手册第 100 页表 6-1 确认其为 512 KB Flash、LQFP100、-40°C 至 +105°C；VET6 的同表温度上限为 +85°C。[GD32E505xx 数据手册 Rev2.3](https://www.gd32mcu.com/data/documents/datasheet/GD32E505xx_Datasheet_Rev2.3.pdf)

SEGGER 当前公开列表包含 GD32E505VET6，但没有列出完整 VET7 型号，不能宣称已获得精确型号支持。[SEGGER GigaDevice 支持表](https://www.segger.com/supported-devices/gigadevice/)

本机只读检查 `C:\Program Files\SEGGER\JLink_V878\JLinkARM.dll` 与 `JLink_x64.dll` 的设备名字符串，可见 `GD32E505VE`、`GD32E505VET6`，未见 `GD32E505VET7`。这只是库内字符串证据，不等价于运行时设备枚举或实板验证；`GD32E505VE` 可作为待确认的系列/容量配置候选，须先在设备选择界面或 SEGGER 官方支持答复中确认，不能静默换成 VET6。

## Cortex-Debug 外部服务

`servertype: external` 配合 `gdbTarget: 主机:2331`，服务由用户手动启动；`executable` 用于符号。`attach` 不走下载流程；`launch` 可通过 `loadFiles` 指定实际烧录文件，默认会把 executable 当下载文件。[官方外部服务说明](https://github.com/Marus/cortex-debug/wiki/External-gdb-server-configuration)、[配置字段](https://github.com/Marus/cortex-debug/blob/master/debug_attributes.md)

当前代码先连接目标，再执行 `overrideAttachCommands`；该数组只覆盖 attach 阶段命令，不应再次写 `target remote`。普通 GDB 命令可直接写字符串，MI 命令才加前导 `-`。[启动顺序与命令转换源码](https://github.com/Marus/cortex-debug/blob/master/src/gdb.ts)

外部 controller 的 attach 已使用 `monitor halt`，但 reset 默认用 `monitor reset halt`。J-Link 专用 controller 则采用 `monitor halt`、`monitor reset`。外部模式可显式采用下列覆盖，匹配 J-Link 行为；此为源码推导，未做实机验证。[external controller](https://github.com/Marus/cortex-debug/blob/master/src/external.ts)、[J-Link controller](https://github.com/Marus/cortex-debug/blob/master/src/jlink.ts)

```json
{
  "overrideAttachCommands": ["monitor halt"],
  "overrideResetCommands": ["monitor halt", "monitor reset"]
}
```

## 项目边界

首次安装使用合并 MCUboot 与签名应用的 `IDU-pre-factory.hex`。当前应用符号文件为同次构建的 `build/IDU-pre/mcuboot-iapv3/IDU-pre/zephyr/IDU-pre.elf`，MCUboot 的符号文件为 `build/IDU-pre/mcuboot-iapv3/mcuboot/zephyr/zephyr.elf`；ELF 用于符号与地址映射，不能随意下载未签名 ELF 覆盖签名镜像。见本项目[构建与升级](build-and-upgrade.md)。

板级代码保留 SWD，并仅在 MCUboot 早期将 PC13 浮空；应用恢复 GPIO 喂外部看门狗。由此推断运行后长期断点可能触发外狗复位，必须按实板电路选择调试期禁用方法；普通内核定时器在 CPU halt 时不能继续喂外狗。见 `boards/idu/idu_pre/board.c` 与 `modules/watchdog/src/watchdog.c`。

## Zephyr 远程调试的补充核对

本机存在 `GDBServer/RTOSPlugin_Zephyr.dll`。Zephyr 官方插件仓库要求固件启用 `CONFIG_DEBUG_THREAD_INFO=y`，Server 以 `-rtos <插件路径>` 加载，GDB 才能获得线程视图。[Zephyr RTOS 插件](https://github.com/zephyrproject-rtos/jlink-zephyr)

本地 Zephyr 4.4.2 的 `subsys/debug/Kconfig` 中，`DEBUG_THREAD_INFO` 自动选择 `THREAD_MONITOR` 和 `THREAD_NAME`。现有应用 ELF 有 DWARF 行号和符号，当前未启用线程调试元数据。操作方案中的新配置构建尚未执行。

本地 `scripts/west_commands/runners/jlink.py` 支持 `--gdb-host`；attach 仅连接，debug 会附加 `load`；即使远程连接仍先检查本地 Commander/共享库与 pylink。`debugserver` 拒绝 `--gdb-host`。自定义板目前没有 board.cmake 与生成的 runners.yaml，west runner 尚未接入，直接 SDK GDB 不受此限制。[Zephyr west 工作流](https://docs.zephyrproject.org/latest/develop/west/build-flash-debug.html)

Zephyr 自身 `CONFIG_GDBSTUB` 是目标固件内的调试服务，当前文档说明其后端为串口；本方案采用 Windows J-Link 提供 TCP RSP，不启用目标 gdbstub。[Zephyr GDB stub](https://docs.zephyrproject.org/latest/services/debugging/gdbstub.html)


## V8.78 设备索引错误修正

用户实测日志已复现 `GD32E505VET7` 名称查找失败；失败发生在 USB 探针连接成功之后。
随后仅加载本机 `JLink_x64.dll`，调用 `JLINKARM_DEVICE_GetIndex()`：VET7 返回 -1，`GD32E505VE` 与 `GD32E505VET6` 均返回 974，未调用打开探针或连接目标的接口。
因此默认调试配置改为 `GD32E505VE`，实际产品仍为 GD32E505VET7。前面的字符串证据已由本次运行时名称索引证据补充；此前“通用映射待确认”仅描述当时状态。
该查询不证明实板连接或下载成功，仍待用户验证。


## 最终型号按用户说明修正为 T6

用户后续说明“应该是 T6”，因此脚本默认值及当前操作说明统一采用 `GD32E505VET6`。
此完整名称已由本机 V8.78 设备索引查询确认，索引为 974；相应温度档按厂家表 6-1 改为 -40…+85°C。
前面先使用通用 VE 名称的排查方案由该最终默认值替代；VET7 错误日志保留为历史证据。
