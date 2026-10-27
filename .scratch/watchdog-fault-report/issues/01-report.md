Status: resolved
Blocked by: None

# 简化上报入口

将上报和动作类型归 watchdog，删除公开 status_report_fault，统一应用调用点和验证。

## Answer

- 上报只有 error、description、action 三个参数，阶段由 status 判断。
- status 的唯一私有回调直接记录故障并处理产品动作，不增加转调函数或注册表。
- 主动上报在调用者上下文同步回调，返回回调结果；监控故障仍在 workqueue 通知。
- 任务锁存诊断不受主动上报影响；停止后仍保留回调绑定，CONTINUE 不恢复业务。
- 采集生命周期 40 个独立进程场景通过。
- IO 5 个独立进程场景通过，其中新增不链接 status 的线程/ISR 上报、参数校验、停机后上报及监控不受影响验证。
- 看门狗配置矩阵 50 个独立进程场景通过；初始化失败专门重新编译并确认 1 个场景实际 PASS。
- 真实协议运行故障 16 个独立进程场景通过，覆盖正常、降级、调试和 UART 初始化失败。
- MCUboot + IAPv3 固件编译、签名验证通过；产物位于工作区 build/hex/IDU-pre-mcuboot-iapv3/。
- git diff --check 通过。未进行实板验证。
