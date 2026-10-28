Status: resolved
Blocked by: None

# 验证

删除 DEFAULT_VALUE/FLAGS/CALLBACK/ARRAY 宏、重复容量别名，去掉未启用上下限和零值占位。
产品数组初值直接配置 data.initial.words，清理无用头文件引用。
read_point 单调用包装合并进 dp_get_para；私有 check_value 去掉不可能为空的输入判断。
保留编解码、范围和回调最终值校验、唯一点值锁及 RAM 恢复默认。
私有目录查看函数仍被测试使用，未删除。

DP 与 process native_sim 测试通过；默认 MCUboot + IAPv3 固件构建和验签通过。
覆盖范围、回调修改/错误/越界、并发快照、恢复默认及跳过点行为保持。
日志 /tmp/trim-dp-{build,test}.log、/tmp/trim-process-{build,test}.log、
/tmp/trim-firmware.log。未执行实板验证。
保留用户原有两个未暂存修改块。
