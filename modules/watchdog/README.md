# Watchdog 模块

所有实现放在 `src/watchdog.c`，通过段落注释区分外部 GPIO 喂狗与软件任务监控。
唯一对外头文件为 `include/watchdog/watchdog.h`，接口统一使用 `watchdog_*` 前缀。
不增加兼容头或转调封装，已有函数名及行为保持。

- 应用外部接口：`watchdog_external_init()`、`watchdog_external_error()`、`watchdog_external_stop()`；应用不传 GPIO 或硬件实例。
- 底层 `watchdog_init/error/stop` 多实例接口继续保留，供模块底层验证及显式使用者调用。
- 软件接口：`watchdog_task_monitor_init()`、`watchdog_task_monitor_stop()`，以及任务注册、心跳、退出监控和快照查询。

`CONFIG_WATCHDOG_SOFTWARE` 和 `CONFIG_WATCHDOG_EXTERNAL` 独立启用，产品在 prj.conf 中明确开启。
外部关闭时不编译 GPIO 喂狗实现、不配置或翻转引脚；软件关闭时初始化返回 -EACCES。
任务监控不依赖硬件实例或产品状态，超时不自动停止外部喂狗。status 显式调用初始化，并根据结果决定 HOLD / RESET。

软件监控是系统单例，设备树指定的外部实例由模块持有；底层显式实例由调用方持有。任务上下文首次清零，名称和上下文须常驻。
无活动任务时不启动期限定时器，永久休眠仅退出监控，重新注册恢复。超时和主动停止均不可通过迟到心跳恢复。
超时回调在锁外执行，可能来自 ISR 或调用线程，必须非阻塞且支持 ISR。模块不调用产品接口或保存 Flash。

## 设备树绑定与显式初始化

绑定由模块的 dts_root 提供，板子声明一个启用的节点：

```dts
external_watchdog: external-watchdog {
    compatible = "app,gpio-watchdog";
    feed-gpios = <&gpioc 13 GPIO_ACTIVE_HIGH>;
    feed-interval-ms = <10>;
    status = "okay";
};
```

外部功能开启时，编译检查必须恰好存在一个启用节点，周期必须大于零。
设备树只描述硬件，不注册 SYS_INIT。由 main 调用 status_init，再显式调用 watchdog_external_init 配置引脚和启动周期翻转。
进入 main 或初始化其他内核服务不会启动喂狗。外部初始化每次上电仅尝试一次，成功或失败后再次调用均返回 -EALREADY。
所选 GPIO 必须支持 ISR 安全访问，不能使用需要休眠的总线扩展 GPIO。
初始化错误直接返回并保存在模块；status 收到错误后按启动故障留驻，不反复尝试初始化。
喂狗只是内核定时器到期时的一次 GPIO 翻转，没有喂狗线程。硬件喂狗和软件任务监控相互独立。
关闭 WATCHDOG_EXTERNAL 时，初始化与查询返回 0、停止为空操作，不配置/翻转引脚。
从 idu_io 节点移除旧 watchdog-gpios/watchdog-feed-ms，采样硬件绑定不再承载喂狗配置。
