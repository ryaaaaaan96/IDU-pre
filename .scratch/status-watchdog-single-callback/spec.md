# status 与 watchdog 单回调编排

main 只初始化 status；status 通过 watchdog_init 绑定唯一私有回调并启动监控。
watchdog 不引用 status，不增加注册表或应用层故障包装。
status.c 按状态查询维护、故障记录与动作、看门狗回调与初始化、镜像确认分段。
保留启动失败 HOLD、运行故障 RESET、终止后 CONTINUE 不恢复及同步停喂边界。
初始化顺序保留 watchdog 在 LED 前，确保 LED 初始化失败后仍能继续喂狗。
