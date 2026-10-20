# 通信端口接入

`vport` 负责通用链路和路由。`transmit/src/modbus_transport.c` 将 Zephyr
已组帧的 Modbus 接口接入它，供现有 `collect` / `transmit` 使用；
这部分属于已有协议适配层，不另建模块。IDU 默认启用，应用绑定在
[`app/communication/ports/config.c`](../app/communication/ports/config.c)。

| 逻辑端口 | 链路 | Zephyr 接口 | 用途 |
| --- | --- | --- | --- |
| `IDU_PORT_FORWARD`（1） | `IDU_LINK_RS485`（1） | `idu-forward` 指定的 Modbus 串口 | 现有 USART2 / RS485 转发从站，含 MV-IO 和可选 IAPv3 |
| `IDU_PORT_ACQUISITION`（2） | `IDU_LINK_ACQUISITION`（2） | 默认 `RAW_0` | 虚拟采集主站 |
| `IDU_PORT_VIRTUAL_DEVICE`（3） | `IDU_LINK_VIRTUAL_DEVICE`（3） | 默认 `RAW_1` | 模拟下挂设备 |

选择 `IDU_COLLECT_RTU` 后，采集端口改绑 `idu-collect` 的独立物理 UART，
不再注册模拟设备端口。当前板上 RS485 只做转发，不运行第二个 RTU 主站。
点表 `DEV_TABLE_S.port` 仍是采集总线号：bus 0 由
`idu_acquisition_port_init()` 返回的接口交给 `co_init()`；它不是上述逻辑端口号。

## 数据路径

```text
RS485 RX → Zephyr 帧间隔 / CRC → 完整 ADU → vport RX 队列和路由
                                              ↓
                         原接口工作队列 → transmit / MV-IO / IAP
                                              ↓
RS485 TX ← Zephyr RTU 编码及 DE 控制 ← vport TX 锁和后端 ← 应答 ADU

collect → RAW_0 → vport TX → 模拟链路 → RAW_1 → vport RX → 模拟设备
collect ← RAW_0 ← vport RX ← 模拟链路 ← RAW_1 ← vport TX ← 模拟应答
```

接收必须经 `vport` 回调交接成功，才会执行协议业务；发送必须经路由和后端
完成，才计入成功统计。不是旁路监测，也没有同时保留一条直接处理路径。
未注册的 Zephyr 接口保留原行为，便于其他模块独立使用和测试。

这是**完整 Modbus 报文层的适配**。UART 引脚、波特率、RTU 组帧、CRC 和
RS485 DE 仍由 Zephyr 维护，未新增第二套 UART 驱动。`VPORT_DATAGRAM`
表示这里已经得到完整报文，不表示 UART 是报文硬件。协议适配依赖 Modbus；
`vport` 核心仍不依赖任何协议。未来原始 UART/CAN/socket 后端放在
`modules/vport/backends/`，按链路类型注册；当前尚无 CAN/socket 驱动适配。

AIDC 的 `read_model` 是直接产生样本的内存模型，不是 UPP 通信端口，
因此没有把它包装成假的收发链路。日志控制台、MCUboot 和 MCUmgr SMP
继续使用各自的 Zephyr 服务，不属于应用 CO/TR 端口。

## 配置与并发

1. 启动时 `tr_modbus_transport_bind()` 校验接口名及一对一映射，不启用硬件。
2. `vport_init()` 注册 const 链路、端口、路由表。
3. 调用原有 `modbus_init_*()` / `tr_modbus_init_server()` 启用通信。

绑定和路由表保持到重启；串口地址、格式切换及恢复不改变绑定。
本适配器目前一接口对应一端口、一链路，固定 `peer=0`。不要给同一 UART
再注册第二个后端，也不能用它让多个 Modbus 接口并发接管一个 UART。
未来 CAN 多 ID、网络多连接仍由各自适配器处理。

RX 交接发生在原有协议工作队列：先复制完整 ADU 到 vport 有界队列，
再等待短时交接完成。vport 回调只校验来源、复制数据、唤醒原工作队列，
不运行参数保存、下游等待或 IAP 写 Flash。这保留了转发独立工作队列和
既有配置/恢复排空规则；不会让慢业务堵住 vport 接收线程。

- `TR_MODBUS_TRANSPORT_RX_TIMEOUT_MS` 默认 50 ms。池满、无路由或超时均不执行
  该请求；超时交接有代次校验，旧队列项不能覆盖下一次请求。
- RAW 接收在交接和业务执行期间拒绝第二帧，返回 `-EBUSY`，避免覆盖
  Zephyr 单个 `rx_adu`；发送错误会传回采集客户端。
- `TR_MODBUS_TRANSPORT_TX_TIMEOUT_MS` 默认 3000 ms，包含链路锁等待。
  串口发送等待 TX complete 和 DE 释放；失败前停 TX、清未发缓冲、恢复 RX。
  超时不表示对端未收到字节，不能自动重试写操作。
- 后端只接受当前 Modbus 事务线程发起的发送，不能用任意
  `vport_send()` 覆盖正在使用的 Zephyr `tx_adu`。
- 当前只支持 UART 中断 API，显式排除 `MODBUS_SERIAL_ASYNC_API`。
  RAW 回调必须同步消费或复制 ADU，不能保留其指针。

内部载荷为交接代次、MBAP 字段和 ADU data；不会把内部头发到 RS485。
`TR_MODBUS_TRANSPORT_MTU = CONFIG_MODBUS_BUFFER_SIZE + 8`，默认 vport 缓冲 288
字节可容纳标准完整 ADU（通用 vport 的默认值仍为 256，IDU 配置为 288）。
IAPv3 配置使用 1100 字节 Modbus 缓冲及 1108 字节 vport 载荷。
扩大 Modbus 缓冲时必须相应扩大
`VPORT_MAX_PAYLOAD`，编译断言会检查容量。

`vport_get_stats(IDU_LINK_RS485, &stats)` 可查询转发链路收发统计；
采集和模拟设备分别用对应链路号。`rx_delivered` 表示交接回调已返回，
不等于业务已处理成功；无效 CRC 在进入 vport 前即被 Zephyr 拒绝。

## 适配边界与验证

`transmit/compat` 对 Zephyr 4.4.2 四个 Modbus 源文件做哈希校验，在构建目录
生成含交接/发送完成钩子的副本；不修改 West 管理的源码。升级 Zephyr
时必须复核这些钩子。

`tests/communication/` 使用 Zephyr UART/GPIO emulator 和真实 Modbus
客户端/服务端，验证 RTU 往返、CRC、RAW 双向路由、完整载荷、并发拒绝、
交接超时、TX 错误、DE 释放及配置恢复。`tests/mvio/` 还校验应用采集路径
确实增加 vport 收发计数。这些是模拟器验证，RS485 电气时序和 CAN
仍需实板验证。
