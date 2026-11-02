# vport

协议无关的端口对象与同步字节/事件交付，公开头文件 `<vport/vport.h>`。
APP 持有常驻 `vport_t`，填写发送回调；transmit 登记时调用 `vport_bind()` 绑定唯一接收者。
不维护全局端口目录，不创建线程，不解释 CAN ID、协议、波特率或帧边界。

`vport_input()` 在线程中交付 DATA、IDLE、POLL、ERROR；同一端口由 APP 串行调用。
DATA 支持单字节或连续字节块，传入单调微秒接收时刻；块内不能隐含未上报的时序间隔。
调用返回后输入指针即可失效。接入层应按实际时序分块，不能用线程处理时刻替代接收时刻。
`vport_output()` 同步调用 APP 的带超时发送回调，返回前必须完成或中止，不能保留借用指针。
正返回值归一为 -EIO；ISR 调用返回 -EWOULDBLOCK。

实例、协议顺序及数据处理见 [transmit](../transmit/README.md)。
