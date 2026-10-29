Status: resolved
Blocked by: None

# 管理映射非法恢复

按用户授权，非法设备 ID、重复设备、管理记录版本/长度/状态错误时整区重建一次。
NVS/Flash 读取失败不能触发擦除；重建失败或重建后仍非法直接返回。
单点参数错误处理不变，不新增锁，不提交。

## 验证

写入真实 NVS 管理记录（CRC 正确但内容非法），跨进程验证零 ID、重复、状态、版本和长度恢复。
注入读取失败验证不擦除且原参数保留；持续非法验证仅一次重建，不循环擦除。

结果：dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 30 次场景通过，
周期线程场景通过。真实 NVS 的五种非法管理记录均跨进程恢复为默认参数；
管理读取故障未擦除且保留历史参数，持续非法在两次加载后返回 -EBADMSG。

日志 /tmp/dp-mapping-reset-validation.log、/tmp/dp-final-*-{build,test}.log、
/tmp/dp-storage-periodic-{build,test}.log。默认固件构建及签名校验通过：
/tmp/dp-mapping-reset-firmware.log，应用 Flash/RAM 90228/46080 B。
签名 digest b31a15766753b4771a1d2e10ff347522e4d5a74e6de8df4e25708c99e24a4b00。
clang-format dry-run、ruff、git diff --check 通过；未执行完整回归、三槽变体或实板验证。
没有新增锁，未暂存、未提交。
