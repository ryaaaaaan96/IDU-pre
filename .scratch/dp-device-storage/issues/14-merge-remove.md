Status: resolved
Blocked by: None

# 合并存储删除函数

移除 begin/finish 和包装层，统一 dp_params_remove 直接完成删除。
出厂恢复保留 RAM，不置空 storage；普通设备移除改为 Flash 删除成功后释放 RAM，失败暂停保存。

## 验证

核心/产品装配线性与 hash 均通过；存储两种模式各 45 个跨进程场景及周期保存通过。
默认固件构建和签名通过；clang-format、diff --check 通过。未验证实板，未提交。
日志 /tmp/dp-merge-remove-validation.log、/tmp/dp-final-*-test.log、/tmp/dp-merge-remove-firmware.log。
