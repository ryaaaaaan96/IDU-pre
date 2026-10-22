Status: resolved
Labels: ready-for-agent
Blocked by: None
Type: task

# 引入显式转发映射上下文

## 目标与验收

现有完整报文读写使用指定入口的映射；单入口行为保持。

遵循[正式规格](../spec.md)，从属于[总任务](01-separate-data-and-forwarding.md)。
测试通过完整协议请求、假南向传输和 DP 公共查询验证，不导出私有测试接口。

## 验证记录

已按依赖完成实现；相关完整报文集成测试、table_registry、transmit、points、forwarding 和 shared_rs485 定向回归通过。最终全量验证记录集中在总任务。

## Answer

已完成，接口及使用边界见 docs/forwarding-model.md。
