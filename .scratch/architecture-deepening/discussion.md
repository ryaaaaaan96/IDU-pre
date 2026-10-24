# 采集决策与 RAW 适配收敛讨论

日期：2026-10-24。阶段：设计已确认，进入实施。

## 已确定：采集决策集中

用户采纳将初始化推进、周期到期时间、重试预算、离线探测和恢复调度集中在 collect 内部。
总线任务负责待执行请求选择、通信执行及等待；应用继续管理执行上下文及策略配置。
保持 ADR 0007、0008 和当前运行行为，包括离线探测不额外重试、可配置间隔及显式下发不重试。

以后 status 如需观察采集状态，由 status 主动调用 collect 的状态查询接口。
首版不新增此查询逻辑，也不以此为由改变现有 protocol 状态接入或状态改变日志。

## 官方能力核对与方案选择

用户未接受新增 `modules/modbus_common/` 的提议，要求优先核实 Zephyr 官方基础能力。

已核对当前本地 Zephyr 的 `include/zephyr/modbus/modbus.h`、
`subsys/modbus/modbus_raw.c` 和 `subsys/modbus/modbus_serial.c`：

- 官方提供客户端读写、服务端寄存器回调、自定义功能码、RAW 接收提交和 RAW 发送回调。
- RAW 交接的是 `struct modbus_adu`，不是含 CRC 的连续 RTU 字节帧。
- `modbus_raw_put_header/get_header` 处理 TCP 的 MBAP 头，不能替代 RTU 编解码。
- RTU 编解码在串口实现的内部静态函数中，当前版本没有对应公开的独立编解码接口。
- `modbus_raw_backend_txn` 面向网关转交，会把部分后端失败转换为 Modbus 异常，
  不能未经语义校验就替代采集侧用于区分本地错误、超时和有效异常应答的路径。

因此 CO/TR 可以继续直接复用官方协议能力，但当前完整字节帧的 vport 交接方式仍需要少量适配。
用户最终确认新增 `modules/modbus/`，Zephyr module 标识使用 `modbus_support`。
公共模块提供无内存分配的 RTU 编解码，承接公共串口接口和构建期兼容修正。
CO/TR 直接使用官方协议能力，各自保留 RAW 适配中的事务身份、接收占用和完成规则。
vport 继续协议无关，物理口映射和共享 RS485 编排继续由应用提供。

## 已确定：依赖修正与实施范围

公共兼容实现不引用 CO/TR 目录；转发专用请求选择、广播、整笔写入和工作调度逻辑仍在 transmit。
公共模块调用可选钩子；未启用转发扩展时使用官方等价路径，纯采集构建不依赖 transmit。
编解码由调用者提供缓冲区，内存分配和完成通知由各自 RAW 适配管理。

保持点表接口、IAPv3 线协议、NVS 格式、共享 RS485 编排及现有 k_malloc 所有权约定。
验收覆盖编解码长度/CRC/容量、内存失败清理、完整 ADU、并发身份、离线/恢复、纯采集及固件构建。

官方资料：<https://docs.zephyrproject.org/latest/doxygen/html/group__modbus.html>。
版本事实以当前工作区源码为准；未修改 Zephyr 上游仓库。

## 实施与验收

已完成 collect 内部计划决策集中、公共 Modbus Zephyr module、无分配 RTU 编解码、
CO/TR 调用迁移以及可选转发钩子的依赖修正。status 未新增主动查询。

- `tests/run.sh` 的完整套件分段完成：72 次 native 成功执行、394 项 Ztest 通过，
  12 个任务看门狗独立进程场景通过；主机 C/Python、IAP 30000 个畸形输入与镜像回归通过。
  Python 共 23 项，3 项外部样本跳过。有效验收段无编译警告或测试失败。
- 过程中修正 IAP 独立测试清单漏登记 `modbus_support`；随后完整运行到应用编排夹具时，
  发现其未同步之前状态日志所需的设备容量和 CO 事件通道，补齐后从该套件继续至脚本末尾。
  已通过的前段未重复执行。原始失败日志保留，不能将首次完整命令表述为一次运行通过。
- 默认 MCUboot + IAPv3 联合构建、签名验证及 SGZ 导出通过；应用 Flash 146472 B、RAM 119532 B。
- 独立 `modbus_support` 配置关闭 collect/transmit 仍通过；纯 collect 的客户端回归通过。
- `git diff --check` 及新增架构文档链接检查通过。未执行实板 UART/RS485、IAP 上传或时序测试。

日志在工作区 `build/IDU-pre/architecture-*`，其中 `architecture-full-tests-first.log`
记录第一次清单失败，`architecture-full-tests.log` 记录第二次完整运行及夹具失败，
`architecture-remaining-tests.log` 记录修复后剩余套件，`architecture-validation.log`
汇集两段成功验收，`architecture-firmware.log` 保存固件构建及验签。
