# 领域术语

| 术语 | 含义 |
| --- | --- |
| IDU | 本仓库维护的产品方案：本地 DI/AI 采样、RAM 点值和 MCUboot 升级。 |
| 产品点表 | `app/protocol/tables/process.c` 中的编译期只读 `dp_point_config_t` 行，描述测点身份、类型、初值、范围和回调。 |
| 数据模型 | `dp_table_t`：只读点表指针及数量；登记时提供 device_id，同一表可复用到多个设备。 |
| 本地点 | 由本地采样写入的测点，例如 DI 和电流；登记填默认值，成功写入后更新点值和时间。 |
| 产品信息点 | 型号、版本等普通非持久点，登记填初值，可通过 DP 写入，重启恢复初值；写权限由调用方控制。 |
| 点运行数据 | `dp_point_data_t`：`value_t` 联合体和 `uint32_t updated_at_s`，UNIX 秒，0 为未知。 |
| IAPv3 | 可选的 IAPv3/SGZ 升级协议，由 `modules/iap` 提供；当前产品未接入路由。 |
| 最小原子核心 | DP 只保留模型登记、单点同步读写，不包含采集/转发/异步执行。 |
| signal_key | DP 测点的稳定标识，与 device_id 共同定位测点；与表内行索引独立。 |
| signal_index | process 点表的行顺序，从 0 开始，使用 process_signal_index_t 枚举标识；不参与 DP 存储键。 |
