# dataprocess

[文档导航](../../docs/README.md) · [模块总览](../README.md)

最小原子核心：**模型登记 → 单点同步写 → 单点读 → 本机参数持久化**。
不包含采集/转发描述符、异步样本队列、业务线程、定时器或参考表目录。

## 接口

| 头文件 | 内容 |
| --- | --- |
| `dataprocess/model.h` | `struct dp_model_cfg`、`dp_register_model()` |
| `dataprocess/points.h` | 值/状态、`dp_set_sig_value()`、`dp_get_sig_value()`、`dp_read()`、`dp_stop()` |
| `dataprocess/params.h` | 参数读取、恢复默认、刷新、状态 |
| `dataprocess/reference_tables.h` | 应用点表行 `DM_SIG_CFG_S` 与标志 |

`dp_register_model(model, params_area)` 每次登记一组：校验行定义、按本组实际字数
申请点值块并初始化。业务层按处理表逐组调用，顺序不限；登记错误返回 `-errno`。
`params_area` 由首次调用确定，后续必须一致，否则返回 `-EINVAL`。

参数区在第一次访问参数接口时挂载，不可用时记录状态并继续使用 RAM 默认值。
`DP_NO_PARAMS` 仅用于无 Flash 的测试。

## 多组模型

每个处理表文件持有自己的行数组和模型描述，业务层把指针放进常量数组并逐组登记：

```c
/* app/protocol/tables/process.c */
static const DM_SIG_CFG_S local_rows[] = { ... };
const struct dp_model_cfg protocol_local_model = {
    .device_id = DEVICE_LOCAL,
    .points = local_rows,
    .point_count = ARRAY_SIZE(local_rows),
};

/* app/protocol/tables/remote_process.c */
static const DM_SIG_CFG_S remote_rows[] = { ... };
const struct dp_model_cfg protocol_remote_model = {
    .device_id = DEVICE_REMOTE_IO,
    .points = remote_rows,
    .point_count = ARRAY_SIZE(remote_rows),
};

/* app/protocol/process_runtime.c */
extern const struct dp_model_cfg protocol_local_model;
extern const struct dp_model_cfg protocol_remote_model;

static const struct dp_model_cfg *const product_models[] = {
    &protocol_local_model,
    &protocol_remote_model,
};

int protocol_init(void)
{
    const uint8_t storage_id = DT_FIXED_PARTITION_ID(DT_ALIAS(idu_settings));

    for (size_t i = 0; i < ARRAY_SIZE(product_models); ++i) {
        int err = dp_register_model(product_models[i], storage_id);
        if (err) { return err; }
    }
    return 0;
}
```

约束：

- 每组必须使用不同 `device_id`，重复返回 `-EEXIST`，该组不生效。
- 不同组可以复用相同 `sig_id`，键是 `(device_id, sig_id)`。
- 点数、点值字数和持久参数数量按已登记模型累计校验，超限返回 `-ENOSPC`/`-EINVAL`。
- 逐组登记没有冻结步骤；但参数区一旦挂载，再登记含持久参数的表返回 `-EBUSY`。
  业务层应在读取参数前完成所有登记。
- 每组点值独立申请，登记失败不会回滚之前成功的组。

## 点表行

`DM_SIG_CFG_S` 保留参考平台的字段名，但只解释本产品需要的部分：

| 字段 | 说明 |
| --- | --- |
| `sig_id`, `sig_size`, `data_type` | 信号身份、字节长度和类型 |
| `sig_flag` | `FLAG_CONSTANT`、`FLAG_PERSISTENCE`、`FLAG_RANGE_CHECK`、`FLAG_READ_CALLBACK`、`FLAG_NO_RESTORE` |
| `handler` | 命令点写回调；返回值 0 或 `-errno`，触发值不保存 |
| `read` | 计算/状态点读回调 |
| `data` | 初值、最小值、最大值；数组初值使用静态 `const` 指针 |

- 普通本地点：无标志，采集通过 `dp_set_sig_value()` 写入，质量从
  `UNINITIALIZED` 变为 `GOOD`。
- 常量点：`FLAG_CONSTANT`，初始化即为 `GOOD`，写入返回 `-EACCES`。
- 参数点：`FLAG_PERSISTENCE`，初值写入 NVS；写入同步提交，掉电后恢复。
  `FLAG_NO_RESTORE` 排除在恢复默认之外。
- 命令点：设置 `handler`，只执行回调，不保存触发值。
- 状态点：设置 `FLAG_READ_CALLBACK` 与 `read`，每次读取生成快照。

## 参数存储

`parameter_store.c` 保留原有 NVS 记录和提交镜像格式：每个参数按
`(dev_id, sig_id)` 写入哈希槽，提交镜像在写入值前落盘，掉电后由
`dp_store_open()` 完成或清理。格式不兼容时只清空 `idu-settings` 分区一次；
I/O 错误禁用持久写入但保留缓存读取。

参数写入不再经过下游读回或意图日志；设置值只表示本机期望配置。
`dp_params_prepare_reboot()` 在存储就绪时返回 0，否则 `-EROFS`。

## 构建

加入 `ZEPHYR_EXTRA_MODULES` 并启用 `CONFIG_DATAPROCESS`。模块自动选择
NVS、`NVS_DATA_CRC` 和 `KERNEL_MEM_POOL`，要求 `FLASH`、`FLASH_MAP`、
`FLASH_PAGE_LAYOUT`。

| 配置 | 默认 | 用途 |
| --- | ---: | --- |
| `DP_POINT_WORDS` | 16 | 单点最大字数 |
| `DP_MODELS_MAX` | 4 | 可登记的模型组数 |
| `DP_POINTS_MAX_POINTS` | 32 | 静态状态槽 |
| `DP_POINTS_VALUE_WORDS` | 512 | 点值总字数上限 |
| `DP_PARAMS_MAX_COUNT` | 8 | 持久参数上限 |
| `DP_PARAMS_TRANSACTION_BYTES` | 2048 | 提交镜像上限 |
| `HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS` | 1024 | 点值堆预算 |
| `HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS` | 4096 | 参数缓存/提交堆预算 |
