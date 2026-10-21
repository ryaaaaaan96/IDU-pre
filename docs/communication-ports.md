# 完整报文端口与 RAW 串口接入

应用通过 collect/transmit 注册端口及收发回调；它们的 `src/message.c` 使用
内部通用 `vport`。协议适配分别归 `protocols/modbus/raw.c`，不新增
`modbus_transport` 模块，也不在 vport 中设置 UART/CAN/socket 后端。

## 分层与报文格式

| 位置 | 职责 |
| --- | --- |
| `app/protocol/ports.c` | 虚拟口、来源标识、实际发送接口的编排，以及物理接收回调 |
| `collect/src/message.c`、`transmit/src/message.c` | 端口注册、输入、发送和完成通知；隔离各自拥有的端口 |
| 各模块 `protocols/modbus/raw.c` | RAW ADU 与完整 RTU 报文的转换、CRC 校验、请求来源保持 |
| `modules/vport` | 通用端口登记、回调调用、载荷长度检查、每个逻辑口的发送互斥 |
| Zephyr Modbus 构建期适配 | RAW 接收保护、发送错误传递、串口完整帧接入、DE 与 TX 完成 |

当前回调中的字节格式为 `地址 | 功能码 | 数据 | CRC低字节 | CRC高字节`。
它不是 `modbus_adu` 结构体内存，也没有旧的交接代次或 MBAP 包装头。
协议在 RAW 接口运行，物理串口仍保持 RTU 线格式；IAPv3 使用原有扩展大帧。

```text
发送：协议 RAW 回调 → CO/TR src/message → vport → cfg.send → 实际接口
接收：串口完整帧 → cfg → co_port_input/tr_port_input
      → vport 同步交付 → 协议复制 ADU → Zephyr 对应工作队列
```

vport 不建立另一套 RX 队列或工作线程。输入回调必须同步消费或复制数据，
协议处理使用既有 Zephyr 工作队列；转发的耗时命令继续使用独立工作队列。
所有输入/输出缓冲和来源指针只在回调期间有效。

## 当前产品绑定

| 虚拟口 | 协议接口 | 应用选择的实际通道 |
| --- | --- | --- |
| `IDU_PORT_FORWARD`（1） | `RAW_1` 服务端 | `idu-forward` 对应 USART2 / RS485 |
| `IDU_PORT_ACQUISITION`（2） | `RAW_0` 客户端 | 与北向共用 USART2 / RS485 |

设备表 `DEV_TABLE_S.port` 仍表示采集总线编号，不能直接改为虚拟口编号。
`source.link/peer` 是应用定义的不透明信息。应用在发送回调中检查映射，
服务端响应使用原请求来源；采集侧只接受配置对端的响应。

`idu_ports_init()` 用 `co_modbus_port_bind()` / `tr_modbus_port_bind()` 注册常驻
配置，再通过 `mb_serial_open()` 准备实际串口。`co_port_open()` 和
`tr_server_service()` 仍负责协议生命周期；RAW 发送回调由各自协议适配安装。
重复注册同一配置允许重试，替换配置、跨模块抢占同一虚拟口均被拒绝。

`CO_VPORT` / `TR_VPORT` 分别控制通用报文入口；`CO_MODBUS_RAW_PORT` /
`TR_MODBUS_RAW_PORT` 控制 Modbus RAW 适配。纯采集不启用 transmit 服务。
产品不再启用 `CO_SIMULATOR`。模拟从机配置位于 `tests/fixtures/`，仅回归测试编译。

## 同一条 RS485 的编排

`ports.c` 只打开一个物理串口，CO 的 `RAW_0` 与 TR 的 `RAW_1` 共用发送回调。
`idu_collect_bus.transfer` 包装公开的 `co_modbus_transfer()`，持有链路锁直到
收到应答、异常或超时；北向响应使用同一把锁，不能插入未结束的南向事务。
接收回调根据当前南向请求的地址、功能码及响应长度/写回显分流。
RAW 输入完成复制后立即恢复物理接收，因此北向写处理期间能够收到南向回复。
锁和错误退出覆盖发送失败、超时、坏帧与设备异常，不增加 vport 线程或 UART 驱动。

北向读远端测点仍读取 DP 缓存；周期采集独立运行。已有 31500–31502 参数写映射
经 DP/CO 下发到下游 40000–40002，回读确认后才给北向成功响应。
这些参数是原有演示映射，外部模拟从机可实现它们；接实设备时必须按设备协议修改。
本机 IAP/本地参数继续由自身模块处理，并非所有北向请求都会下发。

实测配置：南向 `device.c` 中 `.coll_addr = 1`，IDU 拨码使用 2–16 中的另一个地址；
所有参与者使用设备树指定的相同串口格式（当前 115200 / 8N1）。
本机与南向地址相同会拒绝采集并返回 `-EADDRINUSE`。

这是同线联调拓扑，本机互斥锁不能约束外部主站发送。主站应观察南向请求/回复并
留出空闲窗口；南向事务期间收到不匹配帧会丢弃，不能当成南向响应或新的北向请求。
无法识别时间上已过期、但地址/功能码/内容仍完全相同的 RTU 响应。
性能测试应记录主站请求成功率、超时和周期采集负载；IAP 上传也需保持总线可用。
不要把此配置视为支持任意双主同时发送的隔离网关。

## 串口时序与构建期兼容

当前 Zephyr 4.4.2 没有公开独立的 RTU 收帧/发送接口，因此继续由
`modules/transmit/protocols/modbus/compat` 生成构建目录内的适配源码。
collect 的协议目录也会加载这套构建适配，不因此启用 transmit 的服务或线程。
源文件 SHA-256 和替换锚点保持严格检查，West 管理的源码不修改。

`modbus_io.h` 暴露串口完整帧接口及 RAW 完成通知。共享实现插入生成的
Zephyr core，沿用原串口配置、定时器、CRC、UART IRQ 和 DE 控制，不新增
第二套 UART 驱动。`core_io.h`、`serial_open.h` 是该构建适配的私有实现片段。

串口接收完成后交给应用回调，由应用调用 CO/TR 的输入入口。RAW 请求处理
结束时，完成通知恢复串口接收，覆盖正常响应、静默请求和失败路径。
发送等待真实 TX complete；超时关闭 TX/DE、清理发送状态并恢复 RX。
物理 UART 只打开一次，串口配置/发送由该实例互斥保护。发送前等待正在组装的
接收帧完成；发送后沿用 Zephyr 配置的 RTU 帧间隔再允许下一次发送。

通过 CO/TR 绑定的 RAW 接口一次只接收一个待处理 ADU，第二帧返回 `-EBUSY`，不覆盖请求或来源。
超长、CRC 错误和错误端口返回错误。RAW 发送回调错误真实传给客户端；
写入超时仍不自动重试，因为不能据此判断远端是否已执行。

`CO_MODBUS_TX_TIMEOUT_MS` / `TR_MODBUS_TX_TIMEOUT_MS` 约束发送预算。
`CONFIG_MODBUS_BUFFER_SIZE` 决定完整帧上限；IAPv3 当前为 1100 字节。
vport 不再分配与此大小相同的共享接收池。

## 扩展与边界

应用注册常驻回调，不在运行期替换端口。多个实际通道可在应用映射到同一个
虚拟口，但须保留来源。实际硬件共享和跨请求事务仲裁由接入实现负责；
当前两个逻辑口按用户指定共用一条 RS485 做性能联调，见下文的总线约束。

采集内置 Modbus 路径继续提供 FC03/04/06/16；自定义功能码可通过已有
`co_bus_cfg.transfer` 接入。Zephyr 4.4.2 的 `modbus_raw_backend_txn()` 本身仅支持
RTU/ASCII 后端，不能把它视为任意功能码的 RAW 事务接口。

本次不实现 CAN、TCP 或外部多主仲裁。今后接入其他传输时，在应用选择
发送接口，并按需增加相应协议报文转换，vport 核心保持不变。

验证由 `tests/shared_rs485`（实际产品 cfg + UART/GPIO emulator）、`tests/vport`、
`tests/communication`、`tests/collect_modbus` 和现有
点表/参数/IAP 回归完成。UART/GPIO emulator 不代替实板的 DE、帧间隔和
电气时序测量。
