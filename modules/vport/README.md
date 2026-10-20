# 虚拟通信端口

`vport` 在物理收发后端与协议之间提供逻辑端口。核心不引用 UART、CAN、网络、
Modbus、DP 或 IDU 设备号，也不封装 ARC；依赖仅为 Zephyr 内核。
这里的“虚拟”表示逻辑映射，与应用现有的模拟采集后端不是同一概念。

## 核心与后端

采用注册接口，配置组织为三张 const 表：

| 配置 | 作用 |
| --- | --- |
| `vport_link_config[]` | 已准备的物理/传输链路、数据类型、MTU、后端操作和上下文 |
| `vport_port_config[]` | 逻辑端口号、数据类型和协议接收回调 |
| `vport_route[]` | 链路与逻辑端口的收发绑定、可选对端匹配和 CAN 接收过滤 |

`vport_init()` 一次验证并注册全部配置。配置与后端上下文持续有效至重启，
重复初始化返回 `-EALREADY`；失败配置不发布，可以修正后重试。
不支持运行期增删端口。网络连接可由已注册后端管理，通过 `peer` 令牌识别，
不必为每次连接重新注册逻辑端口。

通用端口适配器后续放入 [`backends/`](backends/README.md)，按 Kconfig 选择编译，
调用 Zephyr 标准驱动或 socket API 后注册给核心。产品负责 DTS 引脚、
串口参数、网络端点、CAN ID 及协议回调配置。核心不直接打开硬件。

```text
Zephyr 驱动 / socket
    ↕ 后端 send / vport_rx_submit
物理链路（发送锁、MTU、统计）
    ↕ 路由表
逻辑端口（来源信息、协议接收回调）
    ↕
协议解析 / 事务匹配 → 采集、转发等业务
```

现有 `collect` / `transmit` 的 Modbus 收发通过 `transmit` 内的
[通信适配](../../docs/communication-ports.md)接入，包含 RS485 转发、
虚拟采集及可配置的独立 RTU 采集口。Zephyr 继续负责串口组帧和 DE，
适配器把完整 ADU 作为 DATAGRAM 路由。点表 `device.c.port` 仍是采集总线号。
原始 UART 字节流、CAN 和 socket 驱动适配器仍待实现。

## 数据类型与路由

- `VPORT_STREAM`：UART、RS485、TCP 等有序字节流的片段。回调边界不代表协议帧。
- `VPORT_DATAGRAM`：UDP 或后端已定界的完整报文。核心不拆分、不重组。
- `VPORT_CAN`：数据帧，保留 ID、标准/扩展帧、FD/BRS 标志和有效载荷。
  接受经典 CAN 0..8 字节及合法 CAN FD 长度；尚不提供 RTR/错误帧。

一个逻辑端口可以关联多条相同类型链路；来源中的 `link` 和 `peer` 必须保留。
流协议的组帧状态按 `(link, peer)` 分开，不能拼接不同 UART 或 TCP 连接的半包。
异构链路如 CAN 与 UART，需要上层协议或后端先转换为一致的报文边界再汇合。

一条 CAN 链路可以按 ID/掩码映射多个逻辑端口。使用 `rx_flags_mask` 的
`VPORT_CAN_EXTENDED` 位区分相同数值的标准与扩展 ID。
同一链路上 RX 规则相交会返回 `-EADDRINUSE`，包括通配规则与精确规则重叠；
没有“第一条优先”或隐式复制给多个接收者。需要采集和转发同时参与时，
由该逻辑端口的协议回调按请求、响应、主动上报进行分发。

`match_peer` 可按后端提供的 64 位令牌分流不同连接/对端，并限制发送目标。
令牌由后端解释，不是裸指针，也不是核心定义的 IP 地址或 CAN ID。
网络后端应包含连接代次，断开后拒绝过期令牌，避免把旧响应发送给新连接。

发送必须显式指定 `port/link/peer`，只允许已绑定的 TX 路由；没有自动广播、
故障切换或重试。CAN 发送 ID 由协议通过 `frame.id` 指定，接收过滤器不改写发送 ID。
`vport_reply()` 复用保存的来源链路与对端，协议仍负责构造正确响应。

## 最小注册示例

下面的 `can_backend_send`、`can_context`、`telemetry_receive`、`control_receive`
由适配器或协议层提供，不是已经实现的驱动函数。

```c
#include <vport/vport.h>

static const struct vport_backend_ops can_ops = {
    .send = can_backend_send,
};
static const struct vport_link_config links[] = {
    {.id = 1, .type = VPORT_CAN, .mtu = 8,
     .ops = &can_ops, .context = &can_context},
};
static const struct vport_port_config ports[] = {
    {.id = 10, .type = VPORT_CAN, .receive = telemetry_receive},
    {.id = 11, .type = VPORT_CAN, .receive = control_receive},
};
static const struct vport_route routes[] = {
    {.port = 10, .link = 1, .directions = VPORT_RX | VPORT_TX,
     .rx_id = 0x180, .rx_mask = 0x7f0, .rx_flags_mask = VPORT_CAN_EXTENDED},
    {.port = 11, .link = 1, .directions = VPORT_RX | VPORT_TX,
     .rx_id = 0x280, .rx_mask = 0x7f0, .rx_flags_mask = VPORT_CAN_EXTENDED},
};
static const struct vport_config config = {
    .links = links, .link_count = ARRAY_SIZE(links),
    .ports = ports, .port_count = ARRAY_SIZE(ports),
    .routes = routes, .route_count = ARRAY_SIZE(routes),
};

/* 后端准备好后调用，成功后才开启驱动接收。 */
int communication_start(void)
{
    return vport_init(&config);
}
```

## 收发与并发契约

接收使用一个 `k_mem_slab` 缓冲池、一个 `k_fifo` 和一个专用线程，逻辑端口不各自
分配线程。`vport_rx_submit()` 可从 ISR/线程调用，复制输入后立即返回；后端必须按
同一链路的接收顺序串行提交。回调在 RX 线程执行，不持有任何物理发送锁。
配置可用于真实驱动、模拟器或测试，无需更换路由实现。

共享池满时返回 `-EAGAIN`，数据不覆盖已有报文。每条 RX 路由的 `sequence`
按 `uint32_t` 回绕递增，包含因缓冲满而丢失的输入；超长输入也会标记缺口。
后端发生硬件溢出、流数据丢失等情况时，应在后续输入前调用 `vport_rx_gap()`。
它推进相关 RX 路由序号，不立即触发回调。流协议发现缺号后清理半包并重新同步。
序号属于路由；多个 peer 共用通配路由时，应保守清理该路由所有相关组帧状态。
连接重建需更新 `peer` 令牌，不能沿用旧连接的半包。

回调中的来源、帧及数据只在本次调用期间有效。延后回复须复制 `vport_source`，
延后解析须另外复制载荷。回调不能等待该 RX 线程后续投递，也不应执行长时间 I/O；
慢回调会阻塞全部逻辑端口，背压会反映在统计中。

发送在调用者线程执行，每条物理链路有一个可重入 `k_mutex`。
多个逻辑端口共享同一链路锁，不同链路可独立发送。`send` 后端必须在返回前完成
同步发送，或在失败时妥善终止/隔离未完成操作，不能保留输入指针。
异步 Zephyr 驱动由适配器等待完成并处理超时清理。发送超时不保证远端未收到报文，
核心不会自动重试写命令。

`vport_claim()` / `vport_release()` 允许协议线程持有链路完成
“发送请求—等待响应”的整个事务；持锁线程可以调用 `vport_send()`，RX 仍正常投递。
不要在 RX 回调里等待同一线程处理响应。共享同一硬件发送资源的业务必须引用同一
link，不能给同一个 UART 注册两个独立 link 来绕过互斥。锁只协调本机调用者，
不实现标准 Modbus RTU 总线的多主仲裁，也不消除 CAN 或 RS485 的物理带宽限制。

`timeout` 包含取得发送锁的等待时间，后端收到剩余预算。`K_NO_WAIT` 语义须由后端
保持；不支持无等待发送的后端应返回错误，不偷偷阻塞。
`tx_completed` / `tx_errors` 统计实际调用后端的结果，不包含参数拒绝或锁等待失败；
成功表示后端同步发送完成，不等于协议层收到应答。
`rx_queued` 表示已复制入队，`rx_delivered` 表示回调返回；
`rx_unrouted` 表示无接收匹配，`rx_dropped` 统计池满、超长输入及显式缺口报告次数。

## 集成与验证

通过 `ZEPHYR_EXTRA_MODULES` 注册本目录或使用上层 `modules.cmake`，
启用 `CONFIG_VPORT=y`。核心在独立工程中默认关闭；
IDU 通过 `CONFIG_TR_MODBUS_TRANSPORT=y` 默认启用它，收发缓冲和线程会占用 RAM。
容量由 `VPORT_MAX_LINKS / MAX_PORTS / MAX_ROUTES / MAX_PAYLOAD / RX_DEPTH`
配置。默认最大载荷 256 字节，网络后端必须按实际需求配置 MTU；流可按序分块，
报文超长会拒绝，不做透明分片。回调栈由 `VPORT_STACK_SIZE` 控制。
本模块不分配堆内存；一个 RX 缓冲在回调执行期间仍占用池容量。

从工作区执行：

```bash
source .venv/bin/activate
CCACHE_DISABLE=1 ZEPHYR_TOOLCHAIN_VARIANT=host west build \
  -b native_sim/native/64 applications/IDU-pre/tests/vport \
  -d build/IDU-pre/tests/vport
build/IDU-pre/tests/vport/zephyr/zephyr.exe
```

测试工程只加载 `vport` 模块，覆盖 CAN 分流、流来源隔离、peer 匹配、
原路回复、ISR 投递、接收副本与背压、流缺口、物理发送互斥及配置拒绝。
Modbus 适配另由 `tests/communication/` 覆盖，使用 UART/GPIO emulator；
CAN/socket 驱动和实板时序仍需后续接入与验证。
