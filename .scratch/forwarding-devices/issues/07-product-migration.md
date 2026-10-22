Status: resolved
Labels: ready-for-agent
Blocked by: 05, 06
Type: task

# 迁移产品并完成回归与文档

## 目标与验收

移除产品旧关联；完整回归、固件构建、验签及资源记录完成。

遵循[正式规格](../spec.md)，从属于[总任务](01-separate-data-and-forwarding.md)。
测试通过完整协议请求、假南向传输和 DP 公共查询验证，不导出私有测试接口。

## 验证记录

完成产品调用者、文档和项目约定迁移；完整回归及固件构建、验签、导出通过。
资源增加 IDU Flash 840 B / 静态 RAM 176 B，线程栈数量不变。
详见[验证记录](../validation.md)与[审查记录](../review.md)。

## Answer

已完成；未执行实板时序、栈余量和 NOR 实物掉电测试。
