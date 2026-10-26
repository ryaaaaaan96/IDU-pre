Status: resolved
Blocked by: None

# 分离通用看门狗与产品状态策略

软件任务期限与外部 GPIO 喂狗归 watchdog，status 只配置并处置故障。
两个开关独立，关闭外部时不初始化。迁移调用者，维护文档及回归。

## Answer

已完成；验证结果见 ../validation.md。无上游仓库修改、无自动生产任务登记、无实板测试。
