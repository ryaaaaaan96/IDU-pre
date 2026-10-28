Status: resolved
Blocked by: None

# 恢复初始值字段命名

按用户最新要求，点表及内部配置字段 default_value 恢复为 initial，
保留 write_callback/read_callback。dp_default_value_t 类型名未要求修改，保持不变。
只调整名称，不改变初始化、恢复参数或存储行为。原 ISR 分行仍保留未提交。

初始 points.h 字段有用户行尾空格改动，随字段改名保留为未提交改动。

验证：native_sim dp_core / process_runtime 各 1/1、默认固件构建及验签通过；diff 检查通过。
