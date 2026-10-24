# GDB 目标访问失效与寄存器缓存：2026-10-24

## 范围

用户报告程序仍在运行，但 GDB 给出重复的异常寄存器值；J-Link `monitor regs`
全零，CPUID 和 DHCSR 读取失败。诊断使用 Windows J-Link GDB Server V8.78、
GD32E505VET6、SWD；未加载 RTOS 插件。用户退出 GDB 后，执行不下载的实板对照。
初始诊断未下载固件或显式复位；之后用户明确允许下载准备好的调试应用，见下节。
没有修改产品 C 代码或执行全片擦除。用户的 `app/main.c` 改动保留，未纳入本次提交。
连接和暂停会影响 CPU 执行，不能保证外部看门狗绝不因此触发；不把“未显式复位”写成“绝未复位”。

## 已确认的现象

1. 旧服务中，`python3 build/IDU-pre/gdb-access/probe.py --label before-restart`
   复现 CPUID/DHCSR 不可访问，PC/SP/LR/xPSR 同为 `0xa37ffa40`，退出码 1。
2. 同一板上固件、4000 kHz，重建服务后 CPUID=`0x410fd214`、DHCSR=`0x00030003`，
   PC=`0x08028732`、SP=`0x2000e1a0`，连续重连可读。这个结果是恢复措施的证据，
   不是服务、探针或线路根因已经确定。
3. 目标继续运行后重新连接，GDB 在 `monitor halt` 前缓存 `0xdeadbeef`；
   Server 日志明确记录 `Cannot read register ... while CPU is running`。
   `monitor regs` 已读到正常值，`maintenance flush register-cache` 后 GDB 也恢复正常。
   这是已复现并修复的脚本缺口。
4. 4000 kHz 新服务运行数分钟后，原始底层失效复发：`JLINK_ReadRegs` 返回 `0xffffffff`，
   `JLINK_IsHalted()` 返回 `ERROR`，Server 仍返回成功暂停并发送相同垃圾寄存器值。
   CPUID/DHCSR 再次不可读；清缓存不能修复这一层。原会话直接降到 1000 kHz 未恢复。
5. 重新建立 1000 kHz 服务后，修正的脚本读取成功；30 秒重连仍成功，90 秒重连复发同样的
   CPUID/DHCSR 读取失败。证据是 `stability-1000-{30,90}s.log`；降低速度没有消除问题。
   不再把仅重启服务作为根治方案。修正脚本在复发时的 CPUID 检查中止初始化流程。
6. 只读转储发现板上应用与本地 ELF 不匹配：板上 MCUboot 头声明应用 149344 B，
   当前签名镜像应用 152100 B，header 均为 512 B；`text`、`rodata` 逐字节均不同。
   不能仅根据先前显示“下载完成”断言新固件已生效，也不能直接用当前 ELF 的源码定位诊断旧镜像。
   当前构建目录中没有找到与转储应用载荷完全匹配的旧二进制／ELF。
   转储已使用现有签名密钥通过官方 `imgtool verify` 验证，证明保存的是签名有效的旧应用镜像。

## 修复和主机回归

- `scripts/debug.sh` 在暂停、最终复位后刷新 GDB 的寄存器／栈帧缓存。
- 在第一次复位或下载前实际读取 CPUID；读取失败会中止初始化命令文件，全零／全一值会断开并退出。
  这是访问检查，不替代 MCUboot 签名或下载后字节验证；不保证后续传输期间不会再失联。
- `tests/test_debug_session.py` 使用真实 SDK Arm GDB 连接 Python 实现的本机 RSP 目标。
  覆盖 attach 缓存、下载后的复位缓存、CPUID 不可读、全零及全一值；校验失败路径无复位／Flash 写入。
- 修复前 4 个测试共 5 个失败场景；修复后 4 个测试通过（非法 CPUID 含 2 个子场景）。
  测试接入 `tests/run.sh`；本轮不运行与此主机脚本修改无关的完整固件回归。
- `scripts/debug-server.py --no-rtos` 可启动不加载线程插件的 CLI 服务；保留原默认插件行为。
  已分别检查有／无此参数的 `--dry-run` 命令，并通过 Bash 语法及本次修改的空白检查。
  这个选项便于对照，不代表 CLI 或移除插件能够解决底层失联。

## 用户批准后的调试固件对照

- 使用当前工作区（含用户未提交的 `app/main.c`）和 `conf/debug.conf` 单独构建，
  `CONFIG_APP_DEBUG_SKIP_WATCHDOG=y`；应用外部看门狗和任务期限监控关闭，
  可转换的复位故障改为留驻。没有启用 `CONFIG_DEBUG_THREAD_INFO`。
- 镜像导出到工作区 `build/IDU-pre/gdb-access/debug-artifacts/`，构建域为
  `build/IDU-pre/gdb-access/debug-firmware/IDU-pre/`。构建和签名验证通过。
  用户明确允许后，仅下载构建域的 `zephyr/IDU-pre.signed.hex`，不执行全片擦除。
- 从 `0x08010000` 读回 151664 B，与签名 HEX 的全部字节相同；该连续字节流 SHA-256 为
  `9f259c47c33147ab8c7572bc68a803d987dedb3c605906aec3627c4cce8f3233`。
  它是 HEX 解码后载荷的校验值，不是 HEX 文本文件的校验值。
  导出目录的 `IDU-pre.hex` 再次签名，签名 TLV 字节与构建域 HEX 不同；两者的镜像头及
  应用代码载荷一致。全字节读回对照使用实际下载的构建域 HEX。
- 硬件断点命中本次 ELF 的 `main`（`0x080109bc`）。运行后读取状态为 `STATUS_RUN`，
  `first_fault`／`terminal_fault` 均为 `STATUS_FAULT_NONE`、`last_error=0`、
  `storage_error=0`，镜像确认成功；远端离线数为 2，按既有降级规则运行。
- 连续暂停 15 秒后 PC/SP 未变、CPUID 正常；恢复运行后，GUI V8.78、1000 kHz
  在随后 30 秒检查中再次出现底层访问失败。关闭应用看门狗并未消除复发。
- 同版本改用 CLI、相同固件和 1000 kHz：30／90／180 秒检查正常；15:25:28 的后续
  检查再次失败（Server 日志约启动后 421 秒，距 180 秒检查约 208 秒）。
  GUI／CLI 都会复发；现有记录不能确定它们的失效概率是否不同。

## 底层访问对照与结论边界

GUI 和 CLI 的故障会话中均重复得到：

| 访问路径 | 结果 |
| --- | --- |
| `monitor ReadDP 0` | `0x0BE12477` |
| `monitor ReadDP 1` | `0xF0000040` |
| `monitor ReadAPEx 0 0xFC` | `0x14770015` |
| `monitor ReadMemAP 0 0xE000ED00 1 0` | `0x410FD214` |
| 普通 GDB `x/wx 0xE000ED00` | 无法访问 |

CLI 故障时，读取 AP 的 CSW 为 `0x03800052`；通过已验证的 AP0 写入 DHCSR 的
带钥匙暂停控制值，DHCSR 变为 `0x01030003`（已暂停），而普通寄存器和 CPUID
读取仍失败。随后经同一路径恢复执行，DHCSR 为 `0x05040001`。
这里只写调试控制寄存器，没有修改业务 RAM、Flash 或复位目标。
J-Link 的 `InvalidateCache` 命令也未恢复普通访问。

因此 TCP、SWD/AP 通路仍可用，核心本身仍接受暂停／恢复。失效集中在 J-Link 的
普通核心／内存访问路径；这是定位范围，不足以区分具体的 DLL、探针固件或目标适配缺陷。
不能依据垃圾寄存器值诊断产品栈损坏，也不能宣称已经根治。
重启 V8.78 CLI 后已再次读到正确 CPUID 和 PC/SP，并恢复板上运行；这仍只是临时恢复。

当前板上是上述调试镜像，继续调试需选择同次构建的 ELF。在应用目录运行：

```bash
./scripts/debug.sh --attach --build-dir build/IDU-pre/gdb-access/debug-firmware/IDU-pre
```

下一步是官方新版 J-Link 的版本对照；尚未安装新版或更新探针固件。当前不继续改变应用
故障策略、栈尺寸或 `k_work_cancel_delayable_sync()`。

## 诊断命令更正

最初提供的 `monitor memU32` 命令错误追加了 `1`，被 J-Link 当作写入值；这是诊断命令错误。
随后更正为不带第二个数值的只读形式，并采用 GDB `x/wx` 规避混淆。原始失效早于这些命令。
CPUID 是只读寄存器，DHCSR 写控制位需要高半字 `0xA05F`，`0x00000001` 不满足写钥匙；
按 Arm 规范应忽略，不能由 Server 的 `Writing` 文本确认总线写入实际完成。
这些命令没有涉及 Flash 地址。依据：[Armv8-M 架构手册](https://community.arm.com/cfs-file/__key/communityserver-discussions-components-files/471/DDI0553B_5F00_y_5F00_armv8m_5F00_arm.pdf)
及本地 CMSIS `core_cm33.h` 的 CPUID 只读定义。

## 证据位置

工作区 `build/IDU-pre/gdb-access/`：`before-restart.log`、`after-restart.log`、
`register-refresh.log`、`fixed-running-reconnect.log`、`lower-speed.log`、`fresh-1000.log`、
`server-restart.log`、`server-1000.log`、`flash-compare.log`、`target-slot0.bin`、
`session-red.log`、`session-green.log`；调试镜像对照另见 `debug-download.log`、
`debug-programmed.bin`、`debug-main.log`、`debug-halt-15s.log`、`stability-debug-30s.log`、
`stability-cli-{30,90,180}s.log`、`stability-cli-later.log`、`server-cli.log`、
`cli-raw-ap.log`、`cli-ap-state.log`、`cli-raw-halt.log`、`cli-invalidate-cache.log`、
`server-restored.log`。日志和板上固件转储不提交 Git。

仍未确认底层失效的最终根因；不据此修改 `k_work_cancel_delayable_sync()`、栈尺寸或产品故障策略。
