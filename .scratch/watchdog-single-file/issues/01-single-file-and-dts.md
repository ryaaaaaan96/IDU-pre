Status: resolved
Blocked by: None

# 简化 watchdog 文件结构并绑定设备树

一个实现、一个公开头，保留统一 watchdog_* 命名。引脚归设备树和模块，硬件自动初始化，应用不感知 GPIO。

## Answer

完成；单独的设备树 YAML 绑定作为必要配置文件保留。测试与固件结果见 ../validation.md。
