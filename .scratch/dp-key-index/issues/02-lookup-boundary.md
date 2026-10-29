Status: resolved
Blocked by: None

# 将寻址差异限制在查找阶段

拆为 find_point_by_key 与 find_point_by_index，返回私有临时设备表/行号引用。
共用 set_para/get_para 只接收已定位的行，不接收 bool 或寻址模式。
四个对外接口及错误码、回调、校验、持久化语义保持不变，不增加常驻内存和锁。

验收：现有 DP 核心两种查找配置、产品装配和存储回归通过。

## 验证结果

DP 核心及 process_runtime 在线性、设备 Hash 两种配置下通过；
dp_storage 两种配置各 70 个跨进程场景通过，周期保存场景通过。
默认 MCUboot + IAPv3 构建及签名验证通过。clang-format 和 git diff --check 通过。
未进行实板验证。

日志：/tmp/dp-lookup-refactor-validation.log、/tmp/dp-final-*-test.log、
/tmp/dp-storage-periodic-test.log、/tmp/dp-lookup-refactor-firmware.log。
