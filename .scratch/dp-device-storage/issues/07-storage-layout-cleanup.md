Status: resolved
Blocked by: None

# 整理槽位配置与参数存储

将 params.c 按布局容量、槽位数据、Flash 检查、映射/删除恢复、保存线程和模块接口分段。
统一 PARAMS_SIZE、MANAGEMENT_OFFSET、SLOTS_OFFSET 和 AVAILABLE_SECTORS 等布局命名，
保持数值计算、Flash 格式、接口及锁不变；展开布局检查中合并的返回条件。
Kconfig 将设备数量与堆预算归为容量配置，保存线程参数另成一组，补充布局变更说明。
文档明确 DTS/Kconfig/点表各自配置内容。不新增抽象层，暂不提交。

## 验证

默认六槽：dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 13 次场景通过，周期线程通过。
三槽配置完整 13 次场景通过；七槽配置按预期命中容量编译断言。
默认固件构建和验签通过，签名 digest 与整理前完全相同：
dfab2cc5a369cec405dcce44de1bdb2400caa308d8f00e5690c82c75e598d842。

日志 /tmp/dp-layout-cleanup-validation.log、/tmp/dp-final-*-{build,test}.log、
/tmp/dp-three-slots-{build,test}.log、/tmp/dp-invalid-slots-build.log、
/tmp/dp-layout-cleanup-firmware.log。clang-format dry-run、git diff --check 通过。
未执行完整回归、无上传构建或实板验证；未暂存、未提交。
