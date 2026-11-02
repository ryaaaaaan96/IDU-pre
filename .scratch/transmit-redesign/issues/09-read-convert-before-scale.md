# 读取先转换再放缩

Status: resolved
Blocked by: None

## 变更

按用户最新指令，tr_mapping_read 先请求 DP 按 external_type 输出，再执行正向放缩，
最后以 external_type 编码。明确替代此前读取先放缩后收窄的约定。
calculation_type 暂保留，仅用于逆放缩后写入 DP，写入路径未修改。
同步公共/内部注释、模块契约、当前设计和规格。

## 验证

新增协议路径回归：U32 原值 100000，external_type 为 U16 或 S16，除以 100，
分别输出 344、-310，且读取不改变 DP 原值。
transmit_stream 17/17、protocol_port 1/1 模拟器测试通过，git diff --check 通过。
本轮未重新构建产品固件，未进行实板验证；按用户要求不提交。
