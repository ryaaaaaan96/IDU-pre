# dataprocess

[文档导航](../../docs/README.md) · [模块总览](../README.md)

最小原子核心：**模型登记 → 单点同步写 → 单点读 → 本机参数持久化**。
不包含采集/转发描述符、异步样本队列、业务线程、定时器或参考表目录。

## 接口

| 头文件 | 内容 |
| --- | --- |
| `dataprocess/model.h` | `struct dp_model_cfg`、`dp_start()` |
| `dataprocess/points.h` | 值/状态、`dp_set_sig_value()`、`dp_get_sig_value()`、`dp_read()`、`dp_stop()` |
| `dataprocess/params.h` | 参数读取、恢复默认、刷新、状态 |
| `dataprocess/reference_tables.h` | 应用点表行 `DM_SIG_CFG_S` 与标志 |

`dp_start(models, count, params_area)` 校验模型、按实际字数申请点值、挂载参数区
并冻结配置。参数区不可用时记录状态并继续使用 RAM 默认值；模型错误返回 `-errno`。
`DP_NO_PARAMS` 仅用于无 Flash 的测试。

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
| `DP_POINTS_MAX_POINTS` | 32 | 静态状态槽 |
| `DP_POINTS_VALUE_WORDS` | 512 | 点值总字数上限 |
| `DP_PARAMS_MAX_COUNT` | 8 | 持久参数上限 |
| `DP_PARAMS_TRANSACTION_BYTES` | 2048 | 提交镜像上限 |
| `HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS` | 1024 | 点值堆预算 |
| `HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS` | 4096 | 参数缓存/提交堆预算 |
