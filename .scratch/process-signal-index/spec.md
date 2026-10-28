# process 行索引与测点键

signal_key 为 DP 稳定键，替代 signal_id；保留所有数值和 NVS 编码。
signal_index 为 process 点表从零开始的顺序，由 typedef 枚举及指定下标初始化保证对应。
不引入重复运行表或 DP 索引接口。保留用户已有的两个未暂存修改块。
