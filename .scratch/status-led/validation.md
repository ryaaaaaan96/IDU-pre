# 独立状态指示灯

按用户要求仅将 LED 提取为 app/status/status_led.c/.h，内部状态及更新函数归该文件。
名为 status_led 的 delayable work 在系统工作队列执行，运行时周期更新，故障时提交立即更新。
保留现有错过周期奇偶补偿、GPIO 有效极性、启动/故障常亮、初始化失败不访问引脚及并发故障强制点亮。

status.c 继续分段管理外部看门狗、任务监控和整机状态；其后台工作只查询外部喂狗错误，
间隔明确为 WATCHDOG_CHECK_INTERVAL_MS。两者分别使用系统工作项，不新增线程。
LED 周期启动使用 k_work_schedule，避免推迟已提交的故障立即更新。

验证：系统编排 39、runtime 16、任务监控 12 个独立进程场景通过，共 67 场景。
MCUboot + IAPv3 固件构建和签名验证通过，SGZ 152032 字节。diff 空白检查通过。
未实板验证。日志位于工作区 build/status-led-{acquisition,runtime,watchdog,firmware}.log。
