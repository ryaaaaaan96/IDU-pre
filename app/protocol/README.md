# 产品点表与本地处理

[文档导航](../../docs/README.md) · [DP 模块](../../modules/dataprocess/README.md)

当前产品只运行本地 process：`app/protocol/tables/process.c` 定义一份编译期只读
模型，`app/protocol/process_runtime.c` 的 `product_models[]` 逐组调用
`dp_register_model()`，参数区在首次访问参数接口时挂载。不创建 DP 线程，
也不编译采集/转发模块。

以后增加第二张处理表时，不需要改 DP：新表文件导出自己的 `struct dp_model_cfg`，
在 `process_runtime.c` 声明 extern，并往 `product_models[]` 追加一个指针。
两组必须使用不同 `device_id`，点位、字数和参数预算跨组累计；
示例见 [DP 模块](../../modules/dataprocess/README.md#多组模型)。

## 启动顺序

1. `main.c` 初始化状态、输入和可选 IAP。
2. `protocol_init()` 对 `product_models[]` 中每张表调用一次
   `dp_register_model(&protocol_local_model, storage_id)`。
3. DP 校验 26 行本地模型、申请 63 words 点值、挂载参数区。
4. 主循环调用 `acquisition_cycle()`，通过 `dp_set_sig_value()` 同步写本地测点。

参数区挂载失败只记录状态并继续运行，设置项使用 RAM 初值；`dp_params_get_status()`
通过参数状态点报告存储错误。

## 点表

`tables/process.c` 使用 `DM_SIG_CFG_S` 显式填写每一行：

- `SIGNAL_DI`、`SIGNAL_CURRENT_1..8`：本地点，无标志。
- `SIGNAL_SAMPLE_PERIOD`、`SIGNAL_LABEL`、`SIGNAL_SAVED_ADDRESS`：
  `FLAG_PERSISTENCE | FLAG_RT_STORE`，初值写入 `idu-settings`。
- `SIGNAL_RESTORE_LOCAL/REMOTE`、`SIGNAL_FLUSH_PARAMETERS`、
  `SIGNAL_RECOVER_PARAMETERS`：命令点，`handler = points_parameter_command`。
- `SIGNAL_PARAMETER_STATUS`：`FLAG_READ_CALLBACK`，
  `read = points_parameter_status`。
- 设备型号、协议版本、标签等：`FLAG_CONSTANT`，初始化即为只读有效值。

`FLAG_RANGE_CHECK` 使用行内的最小值/最大值；未开启时使用类型范围。
`FLAG_NO_RESTORE` 排除在恢复默认之外。数组初值必须是静态 `const` 数据或
`NULL`（表示全零）。

## 回调

`app/process/points.c` 只保留三个业务函数：

- `points_sample_period_ms()`：读取持久参数，失败回退到
  `CONFIG_IDU_SAMPLE_PERIOD_MS`。
- `points_parameter_command()`：校验单字命令并执行恢复/刷新/恢复事务。
- `points_parameter_status()`：返回参数状态快照。

命令点只在数值校验通过后执行动作，不保存触发值；同一命令重复写入不会重放旧值。

## 已移除的装配

原 `protocol.c`、`init.c`、`tasks.c`、`ports.c`、collect/transmit 点表和
IAP 协议段已删除。IAP 模块仍保留 `iap_route()`，但当前构建没有调用者；
重新接入通信需要新增一个最小的 Modbus/RAW 路由适配层。
