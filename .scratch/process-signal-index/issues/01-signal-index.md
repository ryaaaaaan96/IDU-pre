Status: resolved
Blocked by: None

# 实现和验证

DP 的 signal_id 改为 signal_key，应用及测试调用同步更新。
新增 process_signal_index_t，以指定枚举下标配置全部 24 行；索引不进入 DP 存储键。
产品标识头改为 signal_keys.h，process_table.h 提供行索引及只读表声明。

验证：dp_core、process_runtime、adc 的 native_sim 测试通过。
process 测试检查枚举行索引到测点键的映射，再验证本地点读写及参数初始化。
默认 MCUboot + IAPv3 构建及验签通过；镜像摘要与修改前相同：
989a47e4e152608600ddf8d7b85aafef01395dfeb428ae5e222f6137d3d831cd。
日志位于 /tmp/signal-{dp,process,adc}-{build,test}.log 和 /tmp/signal-firmware.log。
未执行实板测试。

用户原有 points.c ISR 判断格式、points.h initial 行末空格保持未暂存，不纳入提交。
