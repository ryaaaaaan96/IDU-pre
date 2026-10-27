Status: resolved
Blocked by: 03

# 直接登记设备表

新增 dp_register_devices，一次传入常驻 DEV_TABLE_S 数组与数量；旧设备 getter 回调保留兼容，禁止混用。
当前 process 应用自行装配本地设备目录，不使用 data_device_cfg/dp_data_device_bind，删除公开 getter。
不迁移或修改停用通信实现；不改变点表行、存储键或数据格式。

## 验证

- process_runtime native：1/1 通过，覆盖无效登记、冻结后拒绝重配、直接登记后的参数默认值查询、本地同步读写和停止。
- reference_config native：2/2 通过，验证旧回调路径和与直接登记互斥，原初始化及定时业务保持。
- 标准固件构建、签名验证和 HEX/SGZ 导出通过。
- git diff --check 通过。未进行实板验证。

## Answer

应用持有并装配设备目录，DP 直接消费常驻目录。data_device_cfg 和旧回调仅在保留通信/参考兼容路径存在，本轮不删除这些实现。
