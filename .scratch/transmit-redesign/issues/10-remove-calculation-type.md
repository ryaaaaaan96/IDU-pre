# 删除独立计算类型

Status: resolved
Blocked by: None

## 变更

用户确认删除 calculation_type，通用映射仅保存 DP 身份、external_type 和 scale。
读路径仍先按 external_type 从 DP 取值，再放缩并以该类型编码。
写路径逆放缩后先按 external_type 表示，再提交 DP；中间算术仍使用 int64_t。
删除相应配置、校验与内部函数多余的类型输出参数，同步契约、设计和 ADR 替代说明。

## 验证

新增协议路径测试：U16 1000 逆放缩为 100000，提交 U32 DP 前先截断为 34464；
S16 -1000 逆放缩为 -100000，按 S16 截断为 31072 后提交。
transmit_stream 18/18、protocol_port 1/1 模拟器测试通过；源码无 calculation_type 残留。
默认产品固件构建与镜像校验通过，导出至 build/hex/IDU-pre-mcuboot-iapv3/。
git diff --check 通过。未进行实板验证，按用户要求未提交。
