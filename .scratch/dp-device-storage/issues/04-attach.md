Status: resolved
Blocked by: None

# 收敛设备参数挂载

移除 attach 的重复点表扫描，使用 register_device 已有持久点计数决定是否挂载。
将 NVS 保留 signal_key 校验移至配置校验；普通 RAM 点允许 0xffff。
复用 find_storage(0) 找空槽，统一新旧设备配置/挂载路径，每次只计算一次槽位偏移。
保留布局格式、擦除顺序、映射提交顺序、删除恢复及拓扑错误冻结行为。
不增加接口或锁，暂不提交。

## 验证

dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 11 次场景运行通过，
周期保存测试通过。新增检查覆盖持久点禁止 0xffff、普通 RAM 点允许此 key 且不占存储槽位。
最终日志 /tmp/dp-final-*-{build,test}.log、/tmp/dp-storage-periodic-{build,test}.log，
执行汇总 /tmp/dp-attach-simplify-validation.log。
默认固件构建和签名校验通过：/tmp/dp-attach-simplify-firmware.log，应用 Flash/RAM
90204/46064 B。签名 digest a3586069ca064be11e7171b9705c1b319e6a310680522c627892ff0f1534005c。
clang-format dry-run、git diff --check 通过。未执行完整回归、无上传构建或实板验证。
未暂存、未提交。
