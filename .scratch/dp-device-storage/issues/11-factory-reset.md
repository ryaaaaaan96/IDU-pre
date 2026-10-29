Status: resolved
Blocked by: None

# 区分恢复默认与出厂恢复

默认恢复沿用 RAM 更新及延后保存，跳过标志命名为 SKIP_DEFAULTS。
新增出厂恢复：暂停目标设备、清理槽位、锁外可选回调、冷重启，保留 RAM 点表。
失败禁止旧值回写，允许重试；应用负责先停止业务访问。

## 验证

- dp_core、process_runtime 线性/hash 均通过。
- dp_storage 线性/hash 各 39 个跨进程场景通过；新增正常出厂恢复、NULL 回调、擦除失败后重试及各自跨启动验证。
- 覆盖暂停后拒绝读写/默认恢复、后台不写回、保留其他设备、清除 SKIP_DEFAULTS 持久点。
- 回调创建另一线程 flush 并等待，验证回调在设备锁外；重启由链接包装替代，仅验证调用顺序和冷重启参数。
- 周期保存套件通过；clang-format、ruff 和 diff --check 通过。
- 默认 MCUboot/IAPv3 固件构建及签名通过，应用 Flash 90,236 B、RAM 46,080 B。
- 尚未验证实板重启、断电及日志发送。保留未提交。

日志：/tmp/dp-factory-validation.log、/tmp/dp-final-*-test.log、/tmp/dp-factory-firmware.log。
