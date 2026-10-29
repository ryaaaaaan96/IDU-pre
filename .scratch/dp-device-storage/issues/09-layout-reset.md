Status: resolved
Blocked by: None

# 布局不匹配自动重建

用户确认读取布局成功但不匹配时擦除整个 dp-params 并重建，不保留旧参数。
删除 check_blank 扫描；读取失败不擦除，擦除/写头失败不继续挂载。
不改变单点损坏处理、设备映射记录格式、锁和其他 Flash 区域。不提交。

## 验证

新增场景验证不兼容布局自动重建、全区尾部清除、分区外哨兵保持、默认值登记，
以及读取失败不擦除、擦除失败不写头、写头失败后可以重试。

执行结果：dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash
各 16 次场景通过；周期保存线程场景通过。原有单点格式/范围不兼容仍返回错误，
跨启动正常布局不触发清除，历史值正常恢复。

日志 /tmp/dp-layout-reset-validation.log、/tmp/dp-final-*-{build,test}.log、
/tmp/dp-storage-periodic-{build,test}.log。
默认固件构建和签名验证通过：/tmp/dp-layout-reset-firmware.log，应用 Flash/RAM 90080/46080 B。
签名 digest b3cd75cb863ad9093dab03264a79e37bf486661a5be4b69ef2a1d7e4e0b2c2da。
clang-format dry-run、ruff、git diff --check 通过。
未执行完整回归、三槽变体、实板擦除/断电验证；没有新增锁，未暂存、未提交。
