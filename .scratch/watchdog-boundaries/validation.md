# 验收记录

2026-10-26，本轮仅本地主机/native 与编译验签，无实板烧录。

- `bash tests/task_watchdog/run_modes.sh`：双开、仅软件各 13 个独立进程场景；仅硬件、全关各 1 个配置场景，通过。
- acquisition：39 个独立进程场景，通过。
- `tests/runtime/run.sh`：正常 7、降级 1、调试 7、UART 初始化失败 1，共 16 个独立进程场景，通过。
- io：5 个外部 GPIO 看门狗测试，通过。
- `tests/test_build_configuration.py`：8 项通过，产品不再强制启用看门狗。
- `scripts/build.sh`：MCUboot + IAPv3 构建、镜像验签通过；SGZ 152192 字节。
- 镜像摘要：`d3aea6d39e2aa66f6fee0976f584907651d7b77e2c1462ef722d404f6c762c84`。
- `git diff --check` 通过。

日志位于工作区 build/watchdog-*.log，输出位于 build/hex/IDU-pre-mcuboot-iapv3/。
软件独立测试不依赖外部实例；软件超时快照不再附带硬件错误，硬件错误单独查询。
保留定时器锁外 ISR 安全通知，使复位不依赖系统工作队列调度；不新增生产任务登记。
外部关闭时不初始化/翻转 GPIO；实际外狗禁用电路、断点和复位链须在实板另行验证。
