Status: resolved
Blocked by: None

# 按简化边界显式启动喂狗

设备树绑定不决定启动时机；status 显式调用，watchdog 定时器直接执行 GPIO 操作。
验证初始化前没有 GPIO 操作、开关组合、初始化失败及原故障行为。

## Answer

已取消 SYS_INIT，设备树与文件结构保持；验证全部通过，详见 ../validation.md。
