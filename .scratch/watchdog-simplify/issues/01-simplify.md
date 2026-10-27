Status: resolved
Blocked by: None

# 合并控制与通知路径

验收：删除重复控制字段和第二份故障缓存；四种开关、队列阻塞、IRQ 延时复位、HOLD、GPIO 故障、只读查询及诊断先后顺序行为明确且可验证。保留用户此前的只读回调和四参数接口，不提交代码。

实现：删除 monitor 结构及 initialized/stopped/checking/registered/notified，删除 reset_started、FEED_* 标志组合、第二份故障缓存、collect_feed_events 和 stop_feeding 两条辅助路径。模式统一控制行为，队列负责一次性通知，查询只读，序号仅维护诊断顺序。

验证：watchdog 四种开关及周期/启动矩阵 72 个独立进程通过；IO 10 个场景和 TIMEOUT_64BIT=n 的同组 10 个场景通过；acquisition 41 个场景、runtime 16 个场景通过。新增 IRQ 复位跨健康有效期、纯查询、待处理故障顺序和 GPIO 升级覆盖。
MCUboot + IAPv3 构建及镜像签名验证通过，SGZ 152160 字节，摘要 fc07ab7d7f50b4d6db482eff0db2bcef584cdaed8900b6d474e4b6eb2105ab16。未实板验证。
日志为工作区 build/watchdog-simple-*.log，矩阵沿用 build/watchdog-*.log。git diff --check 通过。

按用户要求未暂存、未提交。

验证期间用户独立调整了 status LED 阶段图案，未覆盖这些改动。上述 acquisition/runtime 结果对应调整前的集成状态；随后对当前工作树补做 MCUboot + IAPv3 构建及签名验证，SGZ 152464 字节，摘要 39d1955a492eb7d71982394cf5e68390497c9505c29f81a9300a1bda0035d58c，日志 build/watchdog-simple-firmware-current.log。新 LED 行为由对应工作单独验收。

按用户后续要求，WATCHDOG_SOFTWARE 增加 default y。使用未显式设置该项的配置完成真实 Zephyr 配置检查，结果为 y；叠加 conf/debug.conf 后软件监控与外部喂狗均为 n。仅配置变化，无新增源码测试，未提交。
