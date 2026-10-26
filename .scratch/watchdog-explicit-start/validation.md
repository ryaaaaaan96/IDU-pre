# 显式启动验收

2026-10-26，仅本地主机/native 和构建验签，无实板验证。

- `bash tests/task_watchdog/run_modes.sh` 通过：双开、仅软件各 13 个独立进程；仅硬件、全关各 1 个场景；初始化失败 1 个场景。
- 所有任务场景在 status_init 前等待 30 ms，确认喂狗引脚配置次数和边沿均为 0；之后按外部开关启动。
- 外部初始化成功或失败后重复调用均返回 -EALREADY，不再次配置 GPIO；关闭功能时返回 0 且不操作硬件。
- GPIO 配置失败直接返回 -EIO，status 初始化进入 HOLD；等待后仍不产生边沿且错误保持。
- acquisition：39 个独立进程场景通过。
- `tests/runtime/run.sh`：正常 7、降级 1、调试 7、UART 失败 1，共 16 个独立进程场景通过。
- `scripts/build.sh`：MCUboot + IAPv3 构建及验签通过；SGZ 152272 字节，镜像摘要 f764b854ee87db0847dfeb6664c47068c64ec10249f945d7fa09da09349979b1。
- `git diff --check` 通过。

日志在工作区 build/watchdog-explicit-*.log 和 build/watchdog-*-tests.log。
不新增喂狗线程、不自动登记业务或启动监控；未登记流程卡住但定时器可运行时，仍会继续硬件喂狗。
硬件波形、MCUboot 引脚交接及复位链须另做实板验证。
