# 关闭产品不用的参考表路径

Status: open
Blocked by: None
优先级：P2

## 目标

产品已改为 `dp_register_models()`，不再通过 `DEV_TABLE_S` 参考目录初始化。
`CONFIG_DP_REFERENCE_TABLES` 和 `CONFIG_DP_TABLE_REGISTRY` 不应再被产品默认强制开启。

## 证据

- `app/Kconfig` 无条件 `default y`：`DP_REFERENCE_TABLES`，以及
  `DP_TABLE_REGISTRY default y if DP_REFERENCE_TABLES`。
- 产品入口 `process_runtime.c` 只登记 `protocol_local_model`；不调用
  `dp_reference_prepare()`，也不调用 `dp_table_registry_bind()`。
- 最终镜像中 `reference_point()` 仍被 `compile_tables()` 引用；`devices[]` 96 B
  仍在 `.bss`；`table_registry.c` 代码未链接，但配置仍要求它存在。
- `TR_FORWARD_CONFIG` 依赖 `DP_TABLE_REGISTRY`；产品当前不运行转发，可随之下调。
  `app/CMakeLists.txt` 的必需服务列表不包含 `TR_FORWARD_CONFIG`。
- `CO_MODEL_SIMULATOR` 依赖参考表，当前未启用。

## 方案

1. 在产品 `prj.conf` 显式设置：
   `CONFIG_DP_REFERENCE_TABLES=n`、`CONFIG_DP_TABLE_REGISTRY=n`。
2. 观察 `TR_FORWARD_CONFIG` 自动变为 `n` 后的构建结果；若仍有模块引用
   `dp_table_registry_find()`，要么保留 registry，要么把转发配置也显式关闭。
3. 把 `app/Kconfig` 的默认值改为只在旧通信装配下开启，避免下一个产品继续继承。
4. 若某些测试依赖参考表，测试自己的 `.conf` 已显式开启，不受产品配置影响。

## 验收

- 默认产品构建通过，`nm` 确认 `reference_point`、`reference_collect`、
  `compile_tables`、`dp_table_registry_find` 不再链接。
- `.bss` 减少约 96 B，Flash 减少量按 map 记录。
- `dp_models`、`process_runtime`、`table_registry`、`reference_config` 测试按各自
  配置通过。
- 若无法关闭 registry，记录具体依赖链，不要强行删除。

## 风险

- 重新启用转发时忘记打开 `DP_TABLE_REGISTRY` 会导致链接失败；这是期望的
  构建期失败，但需要在文档中写明恢复步骤。
- 参考表路径仍是模块的公开能力，不能删除源码，只关闭产品配置。
