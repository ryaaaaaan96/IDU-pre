# 指示灯阶段图案表验收

Status: resolved

## 验证

| 验证 | 结果 |
| --- | --- |
| acquisition native 构建及 run.py | 42 个独立进程场景通过 |
| `tests/runtime/run.sh` | reset 7 + degraded 1 + debug 7 + reset 1，共 16 个场景通过 |
| task_watchdog 默认及四开关/周期矩阵 | 72 个独立进程通过 |
| MCUboot + IAPv3 固件构建 | 应用 Flash 151356 B、RAM 120076 B；SGZ 152464 字节 |
| diff 空白检查 | 通过 |

覆盖启动慢闪、运行快闪、终止故障常亮、状态切换立即刷新、迟到执行不补放边沿、
GPIO 写入期间锁存故障以及初始化失败不访问引脚。任务看门狗矩阵包含 25/200 ms 检查周期、
四种软件/外部开关、初始化失败和启动宽限场景。

固件构建包含 MCUboot 验签和 IAPv3 SGZ 导出；未进行实板 LED 极性、波形或复位链测量。
日志位于工作区 `build/acquisition-led-*.log`、`build/runtime-led.log`、
`build/task-watchdog-led-*.log`、`build/watchdog-*.log` 和 `build/firmware-led.log`。
