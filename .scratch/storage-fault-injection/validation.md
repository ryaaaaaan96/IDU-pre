# 存储故障注入按步骤命中验证

实施基线：`18109a0`。落实复盘第 2 项，仅修改 points 测试夹具和测试说明，
保留用户在 `app/protocol/protocol.c` 中未提交的 getter 格式修改。

## 修改

- `storage_fault.h/.c` 以单次注入选择挂载、写意图、写提交日志、写参数值、写元数据、
  删除提交日志和删除意图七个步骤。定位规则集中维护，不引用产品私有头文件。
- 只包装指定参数分区的外部 NVS 调用；用原子命中记录保证每次 arm 仅返回一次 EIO。
  显式 finish 和套件 after 钩子均检查目标步骤命中，其他较早的错误不能满足该条件。
- 业务验证沿用完整 Modbus 请求、DP 公开状态及 reload/recover；检查故障锁存、
  持久值恢复和南向只写一次。保存失败禁止重启也使用明确的提交日志写入故障。
- 逐字节撕裂写、下发后 Flash 写入中断仍使用真实 Flash simulator 字节回调，
  增加实际故障命中检查，不以步骤注入替代部分写入覆盖。

## 红绿与反向验证

- 挂载和意图写入先用透传包装运行，新断言因目标未命中失败；实现对应包装后通过。
- 其余保存/删除步骤透传时，五个用例均因目标未命中失败；实现定位后全部通过。
- 额外反向控制：选择 intent delete，但临时在较早的 Flash 写入返回 EIO。
  测试以 `Storage fault stage 'intent delete' was not reached` 失败，证明不能误认该路径。
  反向控制修改已撤销，最终源码重新构建并验证。

## 最终验证

工作区运行：

```bash
source .venv/bin/activate
CCACHE_DISABLE=1 ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto \
  -b native_sim/native/64 applications/IDU-pre/tests/points \
  -d build/IDU-pre/tests/points
build/IDU-pre/tests/points/zephyr/zephyr.exe -flash_in_ram
```

- 完整 points 套件 52 项通过、0 失败，1 个跨进程重启探针按原设计跳过。
- 按 `tests/run.sh` 的既有流程，用同一临时 Flash 文件在新进程执行
  `IDU_REBOOT_PHASE=write/recover/verify`，三个阶段均通过，验证保存及未决事务恢复。
- 构建无 warning/error，当前任务暂存内容的 `git diff --cached --check` 通过。
- 日志在工作区 `build/IDU-pre/storage-fault-injection/`，不提交生成产物。
- 改动只编入 points 测试；未改产品源代码、NVS 格式或上游仓库。
  本轮未重跑无关套件、固件构建或实板测试。
