Status: resolved
Blocked by: None

# 验证

set/get 使用 value_t 指针，删除 pack/unpack、长度/容量参数，成功统一返回 0。
写入复制输入后回调，调用方输入不变；读成功才覆盖输出。配置类型和有效长度仍校验。
删除旧小端布局的 U8 高字节/数组尾字节检查，按照指定联合体成员处理本机数值。
应用 DI/AI 和周期直接使用 u16，ADC 替身同步签名，接口和文档同步。

DP、process 模拟测试通过；ADC 14 项通过。
覆盖输入副本、回调最终值范围、失败输出不变、数组成员、32 位边界、并发及恢复默认。
默认 MCUboot + IAPv3 构建及验签通过，Flash 87,516 B、RAM 37,280 B。
日志 /tmp/value-api-dp-{build,test}.log、/tmp/value-api-process-{build,test}.log、
/tmp/value-api-adc-{build,test}.log、/tmp/value-api-firmware.log。
未进行实板验证或完整回归；用户原有两个未暂存修改块保留。
