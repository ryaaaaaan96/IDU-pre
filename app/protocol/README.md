# 产品 process 点表

当前只运行本地 process，protocol_init() 直接登记 protocol_local_table，不挂载参数分区。
点表位于 tables/process.c，包含采样周期、标签、产品信息、地址、DI 位图及八路电流。
所有值仅存 RAM，重启恢复配置初值；原恢复默认命令与参数存储状态点已移除。
采集和转发未启用，IAP 也尚未接入串口路由。

inc/process_table.h 定义 process_signal_index_t，每行使用枚举指定下标。
inc/signal_keys.h 定义稳定 key，DP 通过 device_id + signal_key 读写。
删除三行持久化维护点后现有表为 21 行；索引反映当前表顺序，不用作外部存储键。

只保留 DP_POINT_FLAG_CHECK_RANGE 控制数值范围检查，写权限交给调用方。
数组初值直接填写 initial.words 或 initial.bytes，不使用外部指针。
app/process/points.c 只负责 points_sample_period_ms()；失败回退 Kconfig 默认采样周期。
读写回调能力仍在通用 DP 模块中保留，当前产品无需存储维护回调。
