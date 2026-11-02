Status: resolved
Blocked by: None

# 分离协议装配与 RS485-1 物理接入

## 目标与设计

- 原 port.c/port.h 改为 rs485_1.c/rs485_1.h，物理层不包含 DP、Modbus、IAP 和 status。
- UART/GPIO、中断、FIFO、方向、队列、时间戳和发送超时留在物理接入中。
- protocol.c 负责站号、RTU 静默参数、转发表/IAP 地址段装配、协议线程和运行状态门禁。
- 物理接口为 init(vport)、start()、receive(event)，不新增硬件线程；DATA 借用内存仅到下次读取前。
- 保留接收先后顺序、空闲 POLL、溢出错误、同步发送和终止故障后拒绝新业务的行为。
- 启动先准备硬件，再注册 transmit，再创建挂起线程，最后开放接收并启动线程。
- 产品 process 点表单测直接验证登记和读写，真实装配仍由 protocol_port 套件覆盖，删除旧端口替身。

## 验收

process_runtime、protocol_port、IAP/transmit 组合测试与产品固件构建通过。

## 验证结果

- process_runtime 1/1：产品点表登记、读写及默认周期通过。
- protocol_port 1/1：真实 protocol.c 线程与 rs485_1.c 在 UART/GPIO 模拟器中完成
  读写、方向恢复、字序、放缩、部分写拒绝和终止故障门禁。
- iap-transmit 组合 1/1：产品 IAP 地址段与真实接收器的升级回归通过；该测试不经过物理 UART。
- 默认 MCUboot + IAPv3 构建、签名验证、SGZ 导出通过。
- git diff --check 通过；新物理文件不引用 DP、Modbus、IAP、status 或线程创建接口。
- clang-format 未安装，手动保持现有格式并检查修改的 C/H 不超过 100 列。
- 未进行实板 RS485 方向、连续长帧上传及时序验证。
