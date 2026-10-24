Status: pending
Blocked by: None
Type: task

# 02：真实协议装配集成验证

目标：native_sim 执行产品真实协议装配与任务创建，通过硬件 seam 使用已有仿真 adapter，减少重复实现启动知识的测试夹具。

依据：[规格](../spec.md)。故障行为断言等待对应产品决定，成功启动与既有降级规则已有依据。

## 验收条件

- [ ] 测试执行真实 `protocol/init.c` 与 `protocol/tasks.c`。
- [ ] 验证任务放行、真实接收/配置队列和周期维护任务。
- [ ] 验证初始化失败、远端离线、存储降级与维护错误上报。
- [ ] 验证实际主循环故障出口，而非只检查 mock 调用次数。
- [ ] 继续保留对通用 module 独立行为有价值的夹具和回归。
- [ ] 记录覆盖范围，区分 native 模拟和实物行为。

## Comments

2026-10-24：范围已授权。现有 acquisition 和 shared_rs485 套件通过不代表真实任务装配已被覆盖。
