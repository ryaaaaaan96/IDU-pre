# 任务看门狗合入 status

Status: resolved

## 范围与实现

2026-10-24，用户明确要求把任务看门狗合入 `status.c`，在保留显式登记能力的前提下精简设计。

- 删除 `app/status/task_watchdog/` 下的实现、两个头文件和 CMake 文件。
- 任务接口声明合入 `status.h`，活动链表和任务超时快照归 status 私有实例。
- 删除 `physical`、`on_expired`、`initialized`、`terminal`、`fault_valid`，以及
  `status_tasks_init()` / `status_tasks_halt()`；共用现有整机阶段、硬件实例和锁。
- 期限定时器使用静态初始化；无任务时不启动。故障锁存时停止它，后续外狗停喂和业务
  停止动作仍在状态锁外执行；普通留驻继续喂外狗。
- 有效任务超时快照在释放锁前保存，同时阻止故障处理尚未完成期间的迟到心跳和登记。
  超时处理在释放锁后直接调用整机故障接口，不经过可替换回调。
- 保留登记、心跳、永久休眠、重新登记和诊断；不自动给任何业务任务登记。
- acquisition 测试删除旧内部接口 mock，只替换实际硬件边界；看门狗和 runtime
  测试直接包含 `status.h`，运行真实合并实现。

## 验证

| 验证 | 结果 |
| --- | --- |
| acquisition native 构建及 run.py | 34 个独立进程场景通过 |
| task_watchdog native 构建及 run.py | 12 个独立进程场景通过 |
| `tests/runtime/run.sh` | 20 个真实装配场景通过 |
| `CCACHE_DISABLE=1 ./scripts/build.sh` | Arm MCUboot + IAPv3 构建、验签与导出通过 |
| diff 空白检查 | 通过 |

现有测试覆盖最早期限重排、永久休眠与重新登记、迟到及终止后拒绝操作、并发诊断快照、
无登记项持续喂狗、任务超时复位／留驻、显式故障留驻、调试禁用和 LED 继续策略。
native 运行设置 `ASAN_OPTIONS=detect_leaks=0`，不作为泄漏验证；未跑全仓回归。

日志位于工作区 `build/IDU-pre/status-watchdog-merge/`：`acquisition-{build,test}.log`、
`watchdog-{build,test}.log`、`runtime.log`、`firmware-build.log`。
本轮没有下载实板固件或操作 J-Link。
