# 产品点表与本地处理

[文档导航](../../docs/README.md) · [DP 模块](../../modules/dataprocess/README.md)

当前产品只运行本地 process：`app/protocol/tables/process.c` 定义一份编译期只读
点表，`app/protocol/process_runtime.c` 的 `protocol_init()` 调用
`dp_register_table()`，参数区在首次访问参数接口时挂载。不创建 DP 线程，
也不编译采集/转发模块。

新增设备或处理表时，导出 `const dp_table_t`，启动时调用
`dp_register_table(device_id, &table)`；同表可供多个设备使用，应用无需状态数组或链表。
示例见 [DP 模块](../../modules/dataprocess/README.md#三个主接口)。

## 启动顺序

1. `main.c` 初始化状态、输入和可选 IAP。
2. `protocol_init()` 调用 `dp_register_table(DEVICE_LOCAL, &protocol_local_table)`；
   参数分区由 DP 通过设备树 `dp-params` 别名自动选择。
3. DP 校验 24 行本地点表并分配私有设备节点及点数据；首次访问参数时挂载 NVS。
4. 主循环调用 `acquisition_cycle()`，通过 `dp_set_para()` 同步写本地测点。

参数区挂载失败由读取接口返回错误，应用采样周期回退到默认值；`dp_params_get_status()`
通过参数状态点报告存储错误。

## 点表

`tables/process.c` 使用 `dp_point_config_t` 显式填写每一行：

- `SIGNAL_DI`、`SIGNAL_CURRENT_1..8`：本地点，无标志。
- `SIGNAL_SAMPLE_PERIOD`、`SIGNAL_LABEL`、`SIGNAL_SAVED_ADDRESS`：
  `FLAG_PERSISTENCE`，初值写入 `dp-params`。
- `SIGNAL_RESTORE_LOCAL/REMOTE`：恢复默认命令点，`write_callback = points_parameter_command`。
- `SIGNAL_PARAMETER_STATUS`：`FLAG_READ_CALLBACK`，
  `read_callback = points_parameter_status`。
- 设备型号、协议版本、标签等：`FLAG_CONSTANT`，初始化即为只读有效值。

`FLAG_RANGE_CHECK` 使用行内的最小值/最大值；未开启时使用类型范围。
`FLAG_NO_RESTORE` 排除在恢复默认之外。数组初值必须是静态 `const` 数据或
`NULL`（表示全零）。

## 回调

`app/process/points.c` 只保留三个业务函数：

- `points_sample_period_ms()`：读取持久参数，失败回退到
  `CONFIG_IDU_SAMPLE_PERIOD_MS`。
- `points_parameter_command()`：校验单字命令并执行恢复默认。
- `points_parameter_status()`：返回参数状态快照。

命令点只在数值校验通过后执行动作，不保存触发值；同一命令重复写入不会重放旧值。

## 已移除的装配

原 `protocol.c`、`init.c`、`tasks.c`、`ports.c`、collect/transmit 点表和
IAP 协议段已删除。IAP 模块仍保留 `iap_route()`，但当前构建没有调用者；
重新接入通信需要新增一个最小的 Modbus/RAW 路由适配层。

## process 索引与测点键

`inc/process_table.h` 定义 `process_signal_index_t`，每个枚举对应一行配置。
`tables/process.c` 使用 `[PROCESS_...]` 指定下标；`PROCESS_SIGNAL_COUNT` 表示行数。
需要按行访问时，通过 `protocol_local_table.points[索引]` 取得配置的 `signal_key`，
结合登记设备的 `device_id` 调用 DP。DP 仅感知稳定键，不感知 process 行顺序。
调整枚举顺序不改变持久化键；所有配置行必须有对应的枚举指定初始化。

读写回调统一接收 `const dp_callback_context_t *`，其中 signal_index 对应
process_signal_index_t 的枚举行。DP 查找时直接提供下标，不增加映射表。
回调可通过 value 指针修改本次值；命令点仍不缓存触发值，状态点校验成功后保存快照。
