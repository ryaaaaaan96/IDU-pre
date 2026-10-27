Status: claimed
Blocked by: None

# 参数化延时复位

验收：0 立即、非零定时器到期；延时期间保留可用喂狗；不依赖系统 workqueue；重复请求不能推迟，较短请求可提前；线程/ISR 可调用；uint32_t 最大值不溢出。同步调用、状态处理、测试与文档。

补充边界：业务上报与内部监控共用 handle_fault。回调只读，status 只维护状态/业务/日志；动作由上报方或 watchdog 内部决定，HOLD 和延时 RESET 统一由 watchdog 执行。首个任务成功登记前内部监控故障为 HOLD，登记后为 RESET/200 ms；任务登记由应用负责，通用模块不依赖 watchdog。

验证：

- watchdog 四种开关、25/200 ms 检查周期及启动场景共 70 个独立进程通过，包含延时窗口喂狗、队列阻塞、ISR 请求、重复不延后、短请求提前、0 覆盖及 UINT32_MAX。
- IO 9 个独立进程在默认配置和 CONFIG_TIMEOUT_64BIT=n 下均通过，包含不链接 status 时 RESET 仍执行、未登记时监控故障按 HOLD 留驻、登记后按 RESET。
- acquisition 41 个场景、runtime 16 个场景通过；RESET 用例在 sys_reboot 钩子中校验终止状态。
- MCUboot + IAPv3 固件构建及签名验证通过，应用 Flash 151088 B、RAM 120028 B，SGZ 152192 字节；未实板验证。
- 延时复位改用相对超时，不再要求 CONFIG_TIMEOUT_64BIT；延时上限固定为 60 秒，更长的请求按 60 秒提前。
- 回调改为只读快照，动作由上报方或 watchdog 决定；首个任务登记前监控故障为 HOLD，登记后为 RESET/200 ms。主循环不登记自身，未登记任何任务时保持 HOLD；通用模块不依赖 watchdog。
- 复位定时器到期只原子停喂并取消检查，不获取任务监控锁；任务操作由原子复位标志拒绝，避免中断上下文与持锁线程互锁。
- git diff --check 通过。

日志在工作区 build/watchdog-*.log、build/io-watchdog-rebuild.log、build/io-no64.log、build/acquisition-watchdog.log、build/runtime-watchdog.log 和 build/firmware-watchdog.log。

按用户要求，本轮不暂存、不提交，工作树保留供审阅。

接口收敛为四参数 watchdog_report_fault，删除公开 watchdog_reset；回调只读，动作由调用方或 watchdog 内部决定，最终非 RESET 动作忽略延时。所有调用和定时器测试迁移到故障入口。
