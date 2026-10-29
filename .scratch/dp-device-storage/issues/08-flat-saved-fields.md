Status: resolved
Blocked by: None

# 保存值字段平铺

按用户确认保留指针和有效标记，删除 dp_saved_value_t 包装结构。
dp_point_data_t 直接保存 value_t *saved_value、bool saved_valid 和 dirty。
只为持久点分配 value_t 副本，仍在登记时一次分配、删除时一次释放。
未加载历史记录时指针已分配但 saved_valid=false；成功加载/保存才设为 true。
保存比较、回调、删除、设备槽位、存储格式和锁均保持不变，不提交。

## 验证

dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 13 次场景通过，周期保存线程通过。
覆盖指针已分配但保存无效、历史加载后有效、保存成功后的比较、并发修改、失败重试、删除及重启恢复。
验证发现登记时仅对非持久点复制初值，导致持久点无记录时用零值并触发范围失败；
调整为所有点先复制初值，再按需加载历史。原 process_runtime 已覆盖此场景，修复后重新通过。

日志 /tmp/dp-flat-saved-validation.log、/tmp/dp-final-*-{build,test}.log、
/tmp/dp-storage-periodic-{build,test}.log。
默认固件构建和验签通过：/tmp/dp-flat-saved-firmware.log，应用 Flash/RAM 90164/46080 B。
签名 digest b36c3f272c509286ea681ddafcaabf9224fe8a8e1c72aff4e0cd614c92e3cf1e。
clang-format dry-run、git diff --check 通过。未执行完整回归、三槽变体或实板验证。
没有新增锁，未暂存、未提交。
