Status: resolved
Blocked by: None

# 持久设备数量配置

CONFIG_DP_STORAGE_SLOT_COUNT 默认 6、范围 1–255，取代 CONFIG_DP_STORAGE_SLOT_SIZE。
DTS 只指定总区域，扣除 4 KiB 标记和 8 KiB 管理区后均分完整扇区。
每槽至少两个 4 KiB 扇区，否则编译失败。默认六槽格式不变，不新增锁，不提交。
测试容量边界按配置数量判断，另验证三个槽位及不足容量配置。

## 验证

删除配置后使用 pristine always 重建。
- 默认六槽：dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 13 次场景通过，周期线程通过。
- 三槽配置：完整 13 次存储场景通过（每槽四个扇区，即 16 KiB）。
- 七槽配置：按预期编译失败，命中每槽至少两个扇区的 BUILD_ASSERT。
- 默认产品固件 pristine 构建、签名校验通过，布局和默认签名 digest 与调整前一致。
- bash -n、clang-format dry-run、git diff --check 通过。

日志：/tmp/dp-slot-count-validation.log、/tmp/dp-final-*-{build,test}.log、
/tmp/dp-three-slots-{build,test}.log、/tmp/dp-invalid-slots-build.log、
/tmp/dp-slot-count-firmware.log。默认 digest：
dfab2cc5a369cec405dcce44de1bdb2400caa308d8f00e5690c82c75e598d842。

未执行完整回归、无上传固件或实板验证；未暂存、未提交。
