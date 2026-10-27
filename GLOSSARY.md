# 领域术语

| 术语 | 含义 |
| --- | --- |
| IDU | 本仓库维护的产品方案：本地 DI/AI 采样、点值存储、本机参数持久化和 MCUboot 升级。 |
| 产品点表 | `app/protocol/tables/process.c` 中的编译期只读 `DM_SIG_CFG_S` 行，描述测点身份、类型、初值、范围和回调。 |
| 数据模型 | `struct dp_model_cfg`：设备 ID、点表指针和数量；由 `dp_start()` 登记。 |
| 本地点 | 由本地采样写入的测点，例如 DI 和电流；初始化质量为未初始化，首次写入后为 GOOD。 |
| 常量点 | `FLAG_CONSTANT` 行，初始化即有效，写入被拒绝。 |
| 持久参数 | `FLAG_PERSISTENCE` 行，初值和后续写入保存在本机 `idu-settings` NVS 分区。 |
| 命令点 | 设置 `handler` 的行，只执行恢复/刷新等动作，不保存触发值。 |
| 状态点 | 设置 `FLAG_READ_CALLBACK` 和 `read` 的行，每次读取生成快照。 |
| 点值质量 | `dp_quality_t`，表示 GOOD、未初始化、通信错误、过期或坏值；与数值本身分开。 |
| 参数状态 | `struct dp_param_status`：存储就绪、历史恢复、格式重置、最近错误和序号。 |
| IAPv3 | 可选的 IAPv3/SGZ 升级协议，由 `modules/iap` 提供；当前产品未接入路由。 |
| 最小原子核心 | DP 只保留模型登记、单点同步读写和本机参数持久化，不包含采集/转发/异步执行。 |
