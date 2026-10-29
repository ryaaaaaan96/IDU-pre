Status: resolved
Blocked by: None

# 未登记设备单设备出厂恢复

find_table 返回空时不再提前报错，直接删除历史 Flash，成功回调并重启。
有 RAM 点表时保留暂停保存处理，删除失败仍不回调、不重启。
无历史参数也作为成功处理，符合存储层重复删除语义。

## 验证

核心与产品装配线性/Hash 均通过；存储线性、Hash、三槽配置各 70 个进程场景通过。
周期保存通过。新增登记失败后单设备出厂恢复、擦除失败重试、无历史设备冷重启，
跨启动确认目标参数已清理、其他设备历史值保持。
clang-format、ruff、diff --check 通过。默认固件构建和签名验证通过，未验证实板。
日志：/tmp/dp-unregistered-reset-validation.log、/tmp/dp-final-*-test.log、
/tmp/dp-unregistered-reset-three-slots-test.log、/tmp/dp-unregistered-reset-firmware.log。
用户要求完成后提交累计 DP 修改，不推送。
