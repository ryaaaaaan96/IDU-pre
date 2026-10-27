Status: resolved
Blocked by: None

# 主线程故障留驻打印

实现与验证见规格；保留用户已有 LED 图案和启动流程修改。

## Answer

已在主线程实现留驻循环：正常业务终止后立即打印最近故障，随后每 5 秒查询打印；无快照时打印返回错误。watchdog/status 回调不循环，未增加 work 或线程，RESET 定时器独立运行。保留用户此前简化的启动错误链及本轮自行填写的 k_msleep(50000)。

2026-10-27 验证：19 个运行时场景通过（默认 8、降级 1、debug 9、UART 错配 1），覆盖启动和异步运行 HOLD、主线程未退出、至少两次周期日志、采样/通信停止、后台 LED/外狗继续以及 RESET 定时复位。19 个选定启动场景通过，包含各初始化失败及成功后镜像确认。日志为工作区 build/hold-main-runtime.log、build/hold-main-startup-tests.log。

MCUboot + IAPv3 构建、签名验证及 SGZ 导出通过；SGZ 152928 字节，摘要 49caa1c5ec16edee759168b174800e51186685ea816936a1f168f2f893fb4574。日志 build/hold-main-firmware.log。git diff --check 通过，未进行实板验证。

验证限制：尝试运行完整采集套件时，旧 LED 时序测试 test_fault_during_gpio_write_forces_indicator_on 失败，原先按运行 100 ms/故障常亮编写的断言与用户当前运行 500 ms/故障 100 ms 闪烁图案不一致；保留产品图案，未将完整采集回归计为通过。本轮运行时测试检查后台故障闪烁持续运行，不再要求常亮。
