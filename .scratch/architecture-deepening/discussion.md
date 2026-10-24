# 采集决策与 RAW 适配收敛讨论

日期：2026-10-24。阶段：设计讨论，尚未授权按完整方案修改运行代码。

## 已确定：采集决策集中

用户采纳将初始化推进、周期到期时间、重试预算、离线探测和恢复调度集中在 collect 内部。
总线任务负责待执行请求选择、通信执行及等待；应用继续管理执行上下文及策略配置。
保持 ADR 0007、0008 和当前运行行为，包括离线探测不额外重试、可配置间隔及显式下发不重试。

以后 status 如需观察采集状态，由 status 主动调用 collect 的状态查询接口。
首版不新增此查询逻辑，也不以此为由改变现有 protocol 状态接入或状态改变日志。

## 待确定：RAW 共同实现如何复用

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
下一步讨论采用现有 Modbus 支持位置收敛最小编解码实现，或调整交接约定；尚不选定目录或新接口。

官方资料：<https://docs.zephyrproject.org/latest/doxygen/html/group__modbus.html>。
版本事实以当前工作区源码为准；未修改 Zephyr 上游仓库，未执行新的运行测试或实板验证。
