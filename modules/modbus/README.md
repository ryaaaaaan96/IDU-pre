# Modbus support

Zephyr module 名为 `modbus_support`，公共配置开关为 `MODBUS_SUPPORT`。
CO/TR 的 Modbus 适配启用时自动选择它，也可单独启用。

## 职责与依赖

- `src/rtu.c` 提供完整 RTU 帧校验、解码和编码，使用 Zephyr CRC、字节序及 ADU 类型。
- `modbus_support/transport.h` 提供公共串口完整帧接口及 RAW 完成通知。
- `compat/` 在构建目录生成固定 Zephyr 版本的适配源码，保留源文件哈希与替换锚点检查。
- 转发专用请求选择、整笔写入、广播和工作队列处理仍在 transmit；它选择
  `MODBUS_SUPPORT_SERVER_HOOKS` 并实现公共模块声明的内部钩子。
  未选择该扩展时使用普通 Zephyr 工作提交及请求处理路径。

本模块不依赖 collect、transmit、DP、vport 或产品配置，不创建执行线程或应用路由。
CO/TR 保留各自的 RAW 端口登记、事务身份、接收占用、缓冲区生命周期和完成处理。
应用继续选择物理接口及编排共享链路。

## 编解码契约

公开头文件：`modbus_support/rtu.h`。

| 接口 | 行为 |
| --- | --- |
| `mb_rtu_validate(frame, size)` | 检查长度及 CRC；可在分配内存或占用接收上下文之前调用。 |
| `mb_rtu_decode(frame, size, adu)` | 再次校验并填入调用者 ADU，清零 TCP 事务及协议标识。 |
| `mb_rtu_encode(adu, frame, capacity, size)` | 填入站号、功能码、数据和 CRC，返回完整帧长度。 |

所有缓冲区由调用者持有；内部不分配、提交或发送。输入输出不得重叠，调用期间输入保持稳定。
长度超限或容量不足返回 `-EMSGSIZE`，CRC 错误返回 `-EBADMSG`，空指针返回 `-EINVAL`；
失败不修改输出。RTU 帧长度允许 4 至 `CONFIG_MODBUS_BUFFER_SIZE`，语义及功能码校验归协议处理。
RAW 适配在接收占用前预校验，随后解码再次校验，以保留既有非法帧及忙状态错误的判定顺序。

`modbus_raw_submit_rx()`、`raw_tx_cb`、客户端读写和服务端能力仍来自 Zephyr；
`mb_serial_*`、`mb_raw_set_done` 和内部扩展钩子由本模块提供，不能当作官方接口。

独立回归见 `tests/modbus_support`；完整 RAW/RTU、内存失败、身份及并发见
`tests/communication`，产品共线行为见 `tests/shared_rs485`。
