Status: resolved
Blocked by: None

# 保存失败重试与历史值兼容恢复

保存失败使旧副本失效，避免写回旧值取消重试。登记时缺失记录追加初值，
不兼容或越界警告并追加初值；真实读取/CRC 错误及追加失败不发布点表。
核查未登记设备的清理路径，保留原 factory_reset 行为，通过 remove_device 主动清理。

## 验证

核心/产品装配线性与 Hash 均通过；存储两种模式各 63 个进程场景及独立周期保存通过。
新增验证：实际提交后返回失败，再 set 回旧副本或恢复默认值仍保存；跨启动确认值正确。
缺失初值不经 flush 即跨启动保留；类型/长度不兼容和越界回退后跨启动加载。
读取 EIO/EBADMSG 不追加，初值/回退写入失败不发布，重试或原兼容配置可恢复。
未登记设备历史 Flash 通过 remove_device 清理，其他设备不受影响。
默认固件构建及签名校验通过；clang-format、ruff、diff --check 通过。
未验证实板，未提交。
日志：/tmp/dp-storage-recovery-validation.log、/tmp/dp-final-*-test.log、
/tmp/dp-storage-recovery-firmware.log。
