Status: resolved
Blocked by: None

# 验证

删除只读标志及对应拒写与配置互斥检查，原产品信息点保持初值和其他标志。
DP 核心、process 装配 native_sim 通过；原常量点可写、读回及更新时间通过。
同一模拟 Flash main/verify 通过：普通点重启恢复初值，持久参数恢复和恢复默认正常。
默认 MCUboot + IAPv3 构建及验签通过，Flash 91,732 B，RAM 41,576 B。
日志 /tmp/readonly-dp-{build,test}.log、/tmp/readonly-process-{build,test}.log、
/tmp/readonly-{main,verify}.log、/tmp/readonly-firmware.log。未进行实板验证。
用户原有两个未暂存修改块保留。
