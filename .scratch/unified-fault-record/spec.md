# 故障记录统一归 watchdog

保持唯一上报和查询接口 watchdog_report_fault / watchdog_fault_get。
watchdog 记录最近主动上报或监控故障，通知前保存，非历史日志；监控锁存独立保持防止被普通上报掩盖。
status 删除故障和远端/存储诊断副本，只维护整机阶段、动作和 LED。
业务诊断直接查 CO/DP，删除 protocol_update_status 汇总入口。
status 调用 protocol_stop 使用协议公开头，不手写前置声明。
