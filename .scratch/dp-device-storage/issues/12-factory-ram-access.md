Status: resolved
Blocked by: None

# 出厂恢复仅阻止 Flash 保存

允许普通读写/回调和默认恢复，不要求应用提前停止业务；不释放 RAM 点表。
设备锁串行删除与保存；禁止并发登记/删除设备。删除失败继续禁止保存供重试。
测试擦除期间读写回调、失败后 RAM 操作及 flush 不写回旧参数，跨启动恢复初值。

## 验证

dp_core、process_runtime 线性/hash 均通过；dp_storage 线性/hash 各 45 个跨进程场景通过，周期保存通过。
验证设备锁持有期间 RAM 读写及回调正常、失败后默认恢复可用且 flush 不回写，
全部清除覆盖未登记设备历史参数、失败重试及跨启动重建。
默认固件构建与签名通过；clang-format、ruff、diff --check 通过。
日志 /tmp/dp-factory-all-validation.log、/tmp/dp-final-*-test.log、/tmp/dp-factory-all-firmware.log。
未做实板验证，未提交。

## 追加需求

DP_DEVICE_ID_ALL=0 选择全部设备；整 DP 分区清空包含未登记设备历史值。
测试全部清空、擦除失败后继续 RAM 操作及重试、跨启动重建。
