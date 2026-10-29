Status: resolved
Blocked by: None

# 每槽位维护映射错误

删除 topology_error，全局冻结改为槽位 mapping_error 隔离。
映射写失败保留设备归属，禁止该槽位被视为空闲，也不为同一设备分配另一槽位。
该槽位挂载/删除返回原错误，其他槽位正常；重启按实际持久映射重新确认。
新增故障注入覆盖写入前失败、写入成功后报错、新映射及删除标记，并跨进程恢复。
保持所有已有改动，不增加锁，不提交。

## 验证

dp_core、process_runtime 线性/hash 各一套通过；dp_storage 线性/hash 各 13 次场景通过，
周期保存线程场景通过。

新增覆盖：新映射写失败后同槽位禁止复用，同设备禁止再次分配，但其他设备可登记/删除；
删除标记写失败仅隔离对应槽位，原设备 RAM 读写和参数保存继续可用；
实际已提交后报错的新映射/删除标记，跨启动重新加载并正确恢复，其他设备参数不受影响。

日志 /tmp/dp-final-*-{build,test}.log、/tmp/dp-storage-periodic-{build,test}.log；
执行汇总 /tmp/dp-slot-errors-validation.log。
默认固件构建及签名校验通过：/tmp/dp-slot-errors-firmware.log，应用 Flash 90172 B、RAM 45 KiB。
签名 digest dfab2cc5a369cec405dcce44de1bdb2400caa308d8f00e5690c82c75e598d842。
ruff、clang-format 和 git diff --check 通过。未执行完整回归、无上传构建或实板验证。
没有新增锁，未暂存、未提交。
