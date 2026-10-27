# 工作队列检查、硬件喂狗与统一复位

任务检查移到系统 workqueue；硬件 k_timer 仅按健康标志/有效期执行 GPIO 喂狗。
软件周期 Kconfig，硬件周期设备树；有效期及首次检查宽限为三倍软件周期，只有完整成功检查才续期。
故障锁存，过期结果不能恢复；HOLD 显式允许喂狗，RESET 同步停喂并复位。
删除底层多实例、watchdog_error 和 watchdog_external_*；使用统一初始化/停止/故障回调和必要诊断快照。
提供 watchdog_reset，内部调用 sys_reboot，status 不感知硬件引脚或底层复位函数。
status 保留整机阶段、产品故障策略、LED、协议/IAP 停止和镜像确认，避免通用模块依赖业务。
