Status: resolved
Blocked by: None

# 实现和验证

重复键检查使用临时 uint16_t key 数组、qsort 及相邻比较；原点表行顺序保持不变。
临时内存点数乘以 2 B，检查后释放，申请失败不发布设备。
删除读回调标志，仅由非空 read_callback 启用；其余标志明确命名，保留原位值。
回调方向采用 bool is_write，按用户要求没有增加操作枚举。

验证：dp_core、process_runtime native_sim 测试通过；512 行逆序表的首尾重复被拒绝，
既有索引和读写方向断言通过。同一模拟 Flash main/verify 参数持久化及恢复默认通过。
默认 MCUboot + IAPv3 固件构建及验签通过。未进行实板测试。
日志 /tmp/flags-dp-{build,test}.log、/tmp/flags-process-{build,test}.log、
/tmp/flags-persist-{main,verify}.log、/tmp/flags-firmware.log。
保留用户原有 ISR 格式与 initial 行末空格未暂存改动。
