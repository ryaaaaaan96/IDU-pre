# 将完整类型转换归入 common/value

Status: resolved
Blocked by: None

## 实现

common/value 提供 value_can_convert 与 value_convert；整数互转、同类型数组复制、
非法类型拒绝的规则统一管理。转换支持原地操作，失败不修改输出。
DP 移除 can_convert/convert_value；SET 调用通用转换后进入写回调，
GET 在读回调前调用公共兼容判断，回调与校验完成后调用通用转换。
不调整 DP 对外接口、范围检查、回调顺序或持久化。
同步 common、DP 与通信设计文档和架构说明。

## 验证

DP 原有整数转换矩阵、回调和非法组合测试通过；新增公共接口原地转换、空指针、
非法同类型枚举及失败不修改输出的测试。
dp_core 4/4、transmit_stream 18/18、protocol_port 1/1 模拟器测试通过。
产品固件编译及镜像校验通过，git diff --check 通过。
未进行实板验证，按用户要求不提交。
