Status: resolved
Blocked by: None

# 合并设备登记遍历

用户确认将独立持久点计数与配置/初值校验合并，第一遍完成；
分配、挂载后第二遍完成初值设置、保存副本关联和历史加载/校验。
保持数据结构、锁、已保存值方案及所有既有未提交改动，不提交。

## 验证

dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 11 次场景运行通过，
周期保存场景通过。日志 /tmp/dp-final-*-{build,test}.log、
/tmp/dp-storage-periodic-{build,test}.log，执行汇总 /tmp/dp-register-traversals-validation.log。
clang-format dry-run 和 git diff --check 通过。本轮未重建固件或进行实板验证，未提交。
