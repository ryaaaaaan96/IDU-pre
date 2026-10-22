Status: resolved
Labels: ready-for-agent
Blocked by: None
Type: task

# 装配数据设备与采集目标

## 目标与验收

本地只关联 process；远端 10、11 共用表并独立轮询；非法绑定明确失败。

遵循[正式规格](../spec.md)，从属于[总任务](01-separate-data-and-forwarding.md)。
测试通过完整协议请求、假南向传输和 DP 公共查询验证，不导出私有测试接口。

## 验证记录

已按依赖完成实现；相关完整报文集成测试、table_registry、transmit、points、forwarding 和 shared_rs485 定向回归通过。最终全量验证记录集中在总任务。

## Answer

已完成，接口及使用边界见 docs/forwarding-model.md。
