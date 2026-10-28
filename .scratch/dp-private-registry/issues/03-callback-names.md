Status: resolved
Blocked by: None

# 明确读写回调名称

将点表和内部配置的 write/read 改为 write_callback/read_callback，
类型为 dp_write_callback_t/dp_read_callback_t，同步迁移产品表和测试、文档。
只重命名，不改变回调行为或参数存储。保留原 ISR 分行未提交改动。

追加用户要求：initial 统一改为 default_value，覆盖点表、内部配置和默认恢复路径。

讨论：default 是 C 保留关键字，不能用作字段名，保留 default_value。
验证：native_sim dp_core 和 process_runtime 各 1/1 通过，默认固件构建、验签和
产物导出通过；git diff --check 通过。仅命名变化，镜像摘要与修改前一致。
