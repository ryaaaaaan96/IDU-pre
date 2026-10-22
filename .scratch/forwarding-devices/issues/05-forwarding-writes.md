Status: resolved
Labels: ready-for-agent
Blocked by: 04
Type: task

# 隔离写入并保持参数恢复

## 目标与验收

不同入口写入正确的数据键；回调、一次性命令、持久化及读回恢复保持。

遵循[正式规格](../spec.md)，从属于[总任务](01-separate-data-and-forwarding.md)。
测试通过完整协议请求、假南向传输和 DP 公共查询验证，不导出私有测试接口。

## 验证记录

已按依赖完成实现；相关完整报文集成测试、table_registry、transmit、points、forwarding 和 shared_rs485 定向回归通过。最终全量验证记录集中在总任务。

## Answer

已完成，接口及使用边界见 docs/forwarding-model.md。
