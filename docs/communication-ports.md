# 通信端口接入

`vport` 负责通用链路和路由。`transmit/src/modbus_transport.c` 将 Zephyr
已组帧的 Modbus 接口接入它，供现有 `collect` / `transmit` 使用；
这部分属于已有协议适配层，不另建模块。IDU 默认启用，应用绑定及收发编排回调在
[`app/cfg/ports.c`](../app/cfg/ports.c)。

| 逻辑端口 | 链路 | Zephyr 接口 | 用途 |
| --- | --- | --- | --- |
| `IDU_PORT_FORWARD`（1） | `IDU_LINK_RS485`（1） | `idu-forward` 指定的 Modbus 串口 | 现有 USART2 / RS485 转发从站，使用通用点表及可选 IAPv3 |
| `IDU_PORT_ACQUISITION`（2） | `IDU_LINK_ACQUISITION`（2） | 默认 `RAW_0` | 虚拟采集主站 |
| `IDU_PORT_VIRTUAL_DEVICE`（3） | `IDU_LINK_VIRTUAL_DEVICE`（3） | 默认 `RAW_1` | 模拟下挂设备 |

选择 `IDU_COLLECT_RTU` 后，采集端口改绑 `idu-collect` 的独立物理 UART，
不再注册模拟设备端口。当前板上 RS485 只做转发，不运行第二个 RTU 主站。
点表 `DEV_TABLE_S.port` 仍是采集总线号：bus 0 由
`co_port_open(&idu_collect_port)` 返回的接口交给 `co_init()`；它不是上述逻辑端口号。

## 模块与产品配置

`app/communication` 已删除，执行逻辑按职责归属既有模块：

| 位置 | 内容 |
| --- | --- |
| `modules/transmit/src/server.c` | 多端口生命周期与配置更新 |
| `modules/transmit/src/registers.c` | 状态读取、命令匹配及普通参数写入入口 |
| `modules/iap/src/receiver.c` | IAP 完整报文及能力/版本查询，由寄存器服务扩展回调接入 |
| `modules/collect/src/port.c` | 采集客户端初始化 |
| `modules/collect/src/simulator.c` | 配置驱动的模拟设备及故障注入 |
| `modules/collect/src/model_simulator.c` | 模型默认值后端 |
| `app/cfg/ports.c` | 虚拟口/链路绑定、应用收发回调和 `idu_ports_init()` 注册入口 |
| `app/cfg/forwarding.c` | 转发协议选择及串口设置 |
| `app/cfg/parameters.c`、`simulation.c` | 命令/状态地址、IAP 扩展和模拟初值 |
| `app/process/serial_settings.c` | 产品参数编码到串口设置的业务转换 |

网络和协议配置保存在 Flash，转发状态由 transmit 管理，模拟寄存器值由
collect 的实例保存。新增产品提供自己的配置与业务回调，不修改模块源码。

`cfg/ports.c` 注册的是应用自己的 `route_send()` / `route_receive()`：

- 发送：`route_send(context, port, peer, frame, timeout)` 的 `port` 是虚拟口编号，
  `context` 指向已选物理链路编号。应用用二者查找 `endpoints[]`，调用
  `tr_modbus_transport_send()`，由它访问实际 UART/RAW 接口。
- 接收：硬件适配器先调用 `vport_rx_submit(link, peer, frame)`；核心按路由确定
  虚拟口，再调用 `route_receive(source, frame, user)`。应用用
  `source->port/link` 选择协议接口，将完整来源交给 `tr_modbus_transport_receive()`。
  RX 回调负责分发已收到的数据，不再次轮询硬件或提交同一帧。
- `vport` 保留路由校验、共享缓冲和物理链路锁；采集/转发模块保留协议事务与业务处理。
  应用可以增加回调分支接入新的硬件接口，不能在已经持有某条链路锁时偷偷发送到另一链路。
  跨链路发送须从上层显式调用对应的 `vport_send(port, link, ...)`。

当前 Modbus 适配仍是一对一的完整 ADU 交接，不是通用原始字节发送接口。
发送回调必须使用当前已绑定的虚拟口；错误端口返回 `-EACCES`，不会覆盖其他事务。
未来 CAN/UART/socket 后端可沿用相同的应用回调边界；需要实现各自硬件适配后才能使用。

## 数据路径

```text
RS485 RX → Zephyr 帧间隔 / CRC → 完整 ADU → vport RX 队列和路由
    → cfg.route_receive → 适配器交还原接口工作队列 → 寄存器服务 / IAP

应答 ADU → vport TX 路由和链路锁 → cfg.route_send(虚拟口 ID)
    → 适配器调用 Zephyr RTU 编码及 DE 控制 → RS485 TX

collect → RAW_0 → vport TX → cfg.route_send → RAW_1
    → vport RX → cfg.route_receive → 模拟设备（响应沿反向路径返回）
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

1. `app/main.c` 调用 `idu_ports_init()`；应用先校验路由，再调用
   `tr_modbus_transport_bind()` 绑定硬件/仿真接口，最后用 `vport_init()` 注册应用回调和路由。
   该应用入口可重复调用，注册失败可重试，成功后不更换配置。
2. `co_port_open(&idu_collect_port)` 初始化配置中的真实客户端或可选仿真设备；接口交给 `co_init()`。
3. 周期执行 `tr_server_service(&idu_forwarding, sample.address)`，由 transmit 管理启动、地址/串口参数变化和恢复。

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
交接超时、TX 错误、虚拟口误配拒绝、DE 释放及配置恢复。`tests/vport/` 验证共享链路
发送和原路回复时回调收到正确的虚拟口 ID。`tests/forwarding/` 还校验应用采集路径
确实增加 vport 收发计数。这些是模拟器验证，RS485 电气时序和 CAN
仍需实板验证。
