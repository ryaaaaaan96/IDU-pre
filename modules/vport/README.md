# vport 通用报文端口

vport 只提供端口登记及输入输出回调，不识别硬件类型、Modbus、点表或 CAN ID。
应用通过 collect/transmit 的 `message.h` 接口交互；两者在 `src/message.c`
使用自己的 owner 调用 vport，从而隔离端口所有权。

- `message.h` 定义完整报文、来源、应用收发回调及常驻绑定。
- `vport_register()` 在启动期登记；同一对象可重复登记，运行期不替换。
- `vport_input()` 同步交付输入，接收者负责在返回前消费或复制。
- `vport_output()` 按端口串行调用应用发送函数，传递剩余超时预算。
- `vport_complete()` 通知应用请求已处理，可释放接收状态。

模块不创建线程、接收队列或硬件后端。`VPORT_MAX_PORTS` 是唯一容量配置。
发送函数返回前应完成或妥善中止发送，不得保留借用的报文指针；回调返回
正数按 `-EIO` 处理。输入可以从 ISR 调用，但所注册的协议接收函数也必须
满足 ISR 约束；注册和发送必须在线程中调用。

`source.link/peer` 完全由应用解释；`source.dev_id` 明确传递逻辑设备 ID，
0 表示未知，默认常量为 `VPORT_SOURCE_DEFAULT_DEVICE`。设备 ID 与 `peer`
独立，不改变报文内容，也不自动转换为 Modbus 站号或 CAN ID。
vport 原样传递这些字段，不依赖 DP、设备点表或硬件映射。实际链路的多端口映射、事务仲裁和硬件
互斥由应用选择的接入实现负责，逻辑口的发送锁不能替代物理总线事务锁。

产品接入及 RAW/RTU 边界见 [通信端口](../../docs/communication-ports.md)。
