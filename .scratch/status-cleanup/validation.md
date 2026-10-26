Status: resolved

# status 状态与锁使用精简

## 修改

- 删除任务上下文的重复 `registered` 标志，使用已有状态标志判断登记状态。
- 明确故障动作递增顺序，以动作强度判断升级，保留首错、重复动作幂等及禁止降级规则。
- 合并指示工作相邻的状态读取；任务故障查询在释放状态锁后读取物理看门狗错误。
- 保留线程、工作队列与定时器 ISR 共享状态的自旋锁；不改变业务阶段、降级接口或任务登记范围。

## 验证

- acquisition：39 个独立进程场景通过，新增翻转期间发生故障后强制常亮，以及动作不能降级的检查。
- task_watchdog：12 个独立进程场景通过，补充未登记对象休眠及快照查询的错误与输出保持检查。
- `bash tests/runtime/run.sh`：25 个真实装配场景通过，包含 reset、hold、debug、continue、degraded 和无效 UART。
- `CCACHE_DISABLE=1 ./scripts/build.sh`：Arm MCUboot + IAPv3 构建、签名验证及产物导出通过。
- 日志保存于工作区 `build/IDU-pre/status-cleanup/`；native 使用 `ASAN_OPTIONS=detect_leaks=0`，不作为泄漏验证。
- 未执行全量回归或实板下载、GPIO 波形与复位链验证。

构建及测试包含用户原有的 `status_init()` 初始化检查锁位置改动；该改动保留在工作区，不纳入本次提交。
