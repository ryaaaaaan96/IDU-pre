# 单文件与设备树自动喂狗验收

2026-10-26：实现合并为一个 src/watchdog.c，公开接口合并为 watchdog/watchdog.h。
新增 app,gpio-watchdog 绑定及模块 dts_root；板级与测试改用独立节点，移除 idu_io 的喂狗属性。
POST_KERNEL 优先级 90 自动配置/翻转 GPIO，status 仅查询错误与停止自动实例，不引用引脚。
保留原底层多实例函数供显式调用者和低层测试；不增加包装头文件。

- 四种配置：双开、仅软件各 13 个独立进程场景；仅硬件、全关各 1 个场景，通过。
- 测试在 status_init 前检查 GPIO 已按开关完成配置，确认自动初始化而非由应用初始化。
- 自动初始化 GPIO 失败专用构建/场景：保留 -EIO、status 启动进入 HOLD、不产生喂狗边沿，通过。
- acquisition 39 个独立进程场景通过；runtime 正常 7、降级 1、debug 7、UART 失败 1 共 16 个通过。
- io 5 个低层 GPIO 喂狗测试通过。测试自带 GPIO 驱动，因此关闭 GPIO_EMUL，避免设备重复定义。
- MCUboot + IAPv3 编译、验签通过；SGZ 152240 字节，镜像摘要 45502c118906cdd24ae0c50a1099e1dc4fdafba92360867023a3b84d3b469a4c。
- ADC 测试夹具移除旧喂狗属性，ADC native 测试通过。
- git diff --check 通过。

命令：bash tests/task_watchdog/run_modes.sh、bash tests/runtime/run.sh、scripts/build.sh；
其余按既有 native_sim 构建及 run.py 入口。日志在工作区 build/watchdog-dt-*.log、watchdog-*-tests.log。
仅主机模拟及编译验证，没有烧录实板。外部关闭时不初始化引脚，实际芯片能否禁用仍依赖板级电路。
