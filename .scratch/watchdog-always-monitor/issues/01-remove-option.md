Status: resolved
Blocked by: None

# 删除软件监控配置开关

验收：源码、Kconfig 和实际 .conf 不再引用旧开关；硬件开/关均可登记任务并检查，debug 仍执行软件超时动作；各调用和固件构建通过，不提交。

## Answer

已删除软件监控开关及其条件分支。watchdog_init() 成功后始终启动软件检查，检查周期仍由 WATCHDOG_CHECK_INTERVAL_MS 配置；仅外部喂狗保留 WATCHDOG_EXTERNAL 开关。debug 配置只关闭外部喂狗，软件监控继续运行。

2026-10-27 验证通过：看门狗矩阵 58 个、采集 42 个、IO 10 个、运行时 17 个、32 位超时配置 IO 10 个，共 137 个模拟器用例进程。运行时 debug 覆盖任务超时复位。日志位于工作区 build/watchdog-*.log 和 build/watchdog-always-*.log。

MCUboot + IAPv3 固件构建、签名校验和 SGZ 导出通过；SGZ 大小 152464 字节，镜像摘要为 3b61c88d3790776f85c0fd580fbded020f8759a05980979685c4394476a412cd。未进行实板验证。

git diff --check 通过；按用户要求未暂存、未提交，保留已有改动。
