Status: resolved
Blocked by: None

# 去除重复状态

统一故障查询，删除 status 冗余接口和存储，保留终止动作与监控锁存安全语义。

## Answer

- watchdog_fault_get 返回最近主动上报或监控故障；回调前保存，复位钩子也能查询。
- 内部监控锁存独立于最近故障，后来 CONTINUE 覆盖记录不会恢复已失败的监控。
- status 只保留整机阶段、回调动作和 LED；删除单独故障阶段枚举、故障快照和降级汇总。
- 删除 protocol_update_status，业务状态直接调用 co_device_status / dp_params_get_status。
- status 使用协议公开头调用 protocol_stop；清理该头不需要的 collect/transmit 聚合引用，调用方显式包含依赖。
- acquisition：40 个独立进程场景通过；io：5 个场景通过，新增覆盖记录不解除 GPIO 故障锁存。
- task_watchdog 四种配置、初始化错误、启动宽限和可变周期共 50 个独立进程场景通过。
- runtime：16 个独立进程场景通过；构建配置 8 项通过。
- MCUboot + IAPv3 编译和签名验证通过；实板未验证。
- git diff --check 通过。
