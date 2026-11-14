# vport

协议无关的多通道同步字节／事件路由，公开头文件 `<vport/vport.h>`。
APP 持有一个零初始化的 `vport_t` 和 N 个常驻 `vport_channel_t`，不固定通道数量。
`channel_id` 为本 vport 内唯一的非零通信通道号，不是 UART 外设号、Modbus 站号或 DP device_id。
vport 不解释硬件、CAN ID、协议、波特率或帧边界，不创建线程。

## 初始化与销毁

生命周期为 `INIT -> READY -> STOPPED`；生命周期 API 由 APP 串行调用。

1. INIT：填写 channel_id、send/send_context、可选 source/source_context，
   调用 `vport_channel_add()` 一次注册完整物理通道。
   同一节点不得挂到多个 vport，重复 ID 返回 -EEXIST，重复节点返回 -EALREADY。
2. INIT：`vport_channel_bind()` 绑定唯一接收者；transmit 的 session 创建接口内部完成绑定。
   已有绑定返回 -EALREADY，不允许覆盖。
3. `vport_start()` 检查非空集合及全部 send/receive，再进入 READY；source 可以为空。
4. READY：集合与绑定只读，不支持运行期增删或改绑。
5. APP 停止并等待全部收发调用结束，再调用 `vport_stop()` 进入 STOPPED。
6. INIT 回滚或 STOPPED 清理时，先销毁 session，内部调用 `vport_channel_unbind()`；
   再 `vport_reset()` 整体撤销所有通道，恢复 INIT。无需检查 owner 或逐个卸载。

公共接口共九个：channel_add、channel_bind、channel_unbind、start、poll、input、output、stop、reset，
均带 vport_ 前缀。不提供单通道移除或独立 source 改绑接口。
reset 先校验全部节点：存在接收绑定、在途 input/poll/output 时返回 -EBUSY，不做部分清理。
成功时清除 source/source_context、接收 context、next/owner，保留 channel_id 和 send 配置；
再次注册 pull 通道前须重新填写 source/context。空 INIT 对象可重复 reset。
reset 不停止 UART/DMA，不释放节点、缓冲或 context。

忙检查不能替代生命周期同步。生命周期由调用方串行管理，须先停止生产者和等待全部在途调用结束。
stop 不等待线程、不停止硬件，只关闭后续收发入口；STOPPED 须经 reset 后才能重新装配。
物理端口是否支持重新初始化由对应适配层决定，vport reset 不作保证。
节点的 next、owner、input_busy、output_active、receive/receive_context 由模块维护；
source/source_context 在 add 前由适配层填写，注册后 APP 不直接修改字段。

## 同步收发

`vport_input()` 按事件 channel_id 找到通道并同步调用 receive；
`vport_output(port, channel_id, data, size, timeout)` 选择对应 send。
只有 READY 允许收发，其他状态返回 -EACCES；未知通道返回 -ENODEV，
缺少接收者返回 -ENOTCONN，非法输入返回 -EINVAL，ISR 调用返回 -EWOULDBLOCK。

DATA、IDLE、POLL、ERROR 均带 channel_id 和单调微秒接收时刻。
DATA 支持单字节或连续字节块，块内不能隐含未上报的时序间隔。
输入指针只借用到回调返回；不能用线程处理时刻替代实际接收时刻。
同一通道由 APP 保证事件顺序，不同通道可以并发调用，不持有集合锁执行回调。
同步 send 返回前必须完成或中止，禁止保留缓冲区指针，正返回值归一为 -EIO。

共享协议服务和每通道状态见 [transmit](../transmit/README.md)。

## 注册输入源

输入源在 `vport_channel_add()` 前填入通道。未注册 source 的通道仍支持主动 input，
不影响 start，但调用 poll 返回 -ENOTCONN。

`vport_poll(port, channel_id)` 在线程中同步调用 source，再将事件交付给 receive；
与 `vport_input()` 共用事件校验和交付实现。source 返回 0 表示取得完整事件，-EAGAIN 表示无事件，
-ECANCELED 表示事件已消费但提供者主动丢弃，不交付 receive；
其他负错误原样返回且不交付，非法正返回值归一为 -EPROTO；receive 错误原样返回，不重试或重放。
返回值不标识错误来源，接收、协议及发送故障由各所属层记录原因。
source 事件的 channel_id 必须等于指定通道，否则返回 -EINVAL，不允许借此跨通道路由。
source 可阻塞，阻塞时长由提供者约定；vport 不创建线程，不添加硬件停止或故障恢复策略。

同通道 input/poll 共用原子忙状态，从调用 source 前持有至同步 receive 返回，
并发及递归输入返回 -EBUSY，不会先读取并覆盖借用数据；不同通道可并发。
同通道 pull source 与主动 push input 混用时必须由调用方串行化，
包括 source 阻塞等待期间；竞争返回后由调用方保留事件并决定是否重试。
忙状态不覆盖 output，同通道发送串行及主动 input 的事件顺序仍由调用方保证。
source 借用的数据至少保持到本次 poll 返回，receive 必须在返回前消费或复制。
ISR 不允许 input/poll，生命周期变更前须等待所有 input/poll/output 结束。
输入源随 reset 整体解除注册。
