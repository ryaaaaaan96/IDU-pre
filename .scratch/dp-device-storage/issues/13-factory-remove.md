Status: resolved
Blocked by: None

# 收敛出厂恢复的存储调用

出厂恢复通过 dp_params_remove 一次完成删除标记、槽位擦除和映射删除，不释放 RAM。
保留普通设备移除所需的 begin/finish 及其原有行为，避免改变独立接口的失败语义。

## 验证

存储模拟器 45 个跨进程场景通过；diff --check 通过。日志 /tmp/dp-remove-build.log、/tmp/dp-remove-test.log。未提交。
