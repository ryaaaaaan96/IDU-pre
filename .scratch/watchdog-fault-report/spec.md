# 统一故障上报

其他模块只依赖 watchdog，通过 watchdog_report_fault(error, description, action) 上报。
status_init 绑定唯一回调并启动 watchdog；一个私有回调完成产品处理，不新增应用包装或注册表。
故障阶段由 status 判断。主动上报同步通知，可来自 ISR，返回回调结果。
监控自身故障仍在系统工作队列通知；主动上报不修改其锁存快照或健康有效期。
保留三种动作、初始化禁止继续、留驻保持喂狗、复位同步执行和终止后不可恢复。
