Status: resolved
Blocked by: None

# 验证

改名 dp_set_initial_value，直接复制 data.initial，不调用 unpack。
产品初值数组迁移为内嵌 words，测试字节数组迁移为内嵌 bytes。
DP、process native_sim 及 main/verify 跨进程参数恢复测试通过。
默认 MCUboot + IAPv3 构建与验签通过，Flash 91,540 B、RAM 41,576 B。
日志 /tmp/initial-dp-{build,test}.log、/tmp/initial-process-{build,test}.log、
/tmp/initial-{main,verify}.log、/tmp/initial-firmware.log。未进行实板验证。
保留用户原有两个未暂存修改块。
