Status: resolved
Blocked by: None

# 验证

增加 DP_POINT_FLAG_SKIP_RESTORE 和 dp_restore_defaults(device_id)。
仅目标设备未跳过点恢复初值及时间，跳过点保留值与时间，其他设备不受影响。
沿用唯一点值锁，不调用回调，不增加持久化、初始化入口或产品命令点。
DP native_sim 通过，包含初值、错误设备、跳过及设备隔离断言。
默认 MCUboot + IAPv3 构建及验签通过，未执行实板测试。
日志 /tmp/defaults-dp-{build,test}.log、/tmp/defaults-firmware.log。
用户原有 ISR 格式及 initial 行末空格未暂存改动保留。
