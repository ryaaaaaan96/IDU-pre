Status: resolved
Blocked by: None

# 分离硬件定时器与监控锁

验收：定时器无模块锁；故障发生时间保留；阻塞队列到期停喂、后续通知、HOLD 和 GPIO 失败规则保持。

增加测试：GPIO 翻转内部调用诊断查询，证明定时器不持有同一个模块锁。

验证结果：

- watchdog 开关与周期矩阵 50 个独立进程场景通过。
- IO 6 个独立场景通过，含 GPIO 内查询诊断、HOLD 不恢复 GPIO 故障。
- acquisition 40 个独立场景通过，runtime 16 个独立场景通过。
- MCUboot + IAPv3 固件构建、镜像签名验证通过，SGZ 151888 字节。
- git diff --check 通过。未进行实板或多核硬件验证。

日志：工作区 build/watchdog-atomic-{io,acquisition,runtime,firmware}*.log；矩阵日志沿用 build/watchdog-*.log。
