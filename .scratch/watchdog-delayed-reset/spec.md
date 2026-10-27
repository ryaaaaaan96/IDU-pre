# 一次性定时器延时复位

公开入口为 watchdog_report_fault(int error, const char *description, watchdog_action_t action, uint32_t reset_delay_ms)。删除公开 watchdog_reset，复位仅在模块内部执行。第四参数仅最终动作为 RESET 时有效，其他动作忽略；单位毫秒。0 立即停止监控/喂狗并冷复位；非零停止任务检查但保留可用硬件喂狗，使用一次性 k_timer 到期停喂并直接调用底层冷复位，不依赖系统工作队列，最终复位到期回调不获取任务监控锁。

重复非零请求保留更早的期限，不能延后；0 可以立即覆盖待执行复位。延时使用相对超时，不依赖 CONFIG_TIMEOUT_64BIT，上限 60 秒；更长的请求按 60 秒提前。status 回调只读快照，关闭业务并提交非阻塞日志；动作由上报方或 watchdog 内部决定，RESET 使用本次 reset_delay_ms，内部监控故障默认 200 ms。首个任务成功登记前内部监控故障按 HOLD，登记后按 RESET。业务上报与内部监控通知共用 handle_fault，status 不调用 stop/reset；任务登记由应用负责，通用模块不依赖 watchdog。

延时仅提供日志处理机会，不保证输出完成或参数保存。不引入跨复位记录、保留 RAM 或新持久化接口。GPIO 已失败、硬件喂狗已停用或外部看门狗更早超时时，不能保证等到软件期限。

用户要求本轮只修改和验证，保留工作树供审阅，不创建提交。

控制逻辑及内部故障动作规则已由 ../watchdog-simplify/spec.md 替代，不再按任务登记推断阶段；四参数、只读回调、私有复位及 60 秒延时上限保持。
