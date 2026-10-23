Status: resolved
Labels: ready-for-agent
Blocked by: None

# 01：零登记持续喂狗，解除隐式期限

**What to build:** 由 status 按规格交付零登记持续喂狗的完整路径。

- [x] 没有任务登记且无锁存故障时，外部 GPIO 持续产生喂狗边沿，超过旧主循环期限也不停喂。
- [x] 解除主循环单一进展期限和应用默认 DP 活性超时，不通过自动登记替代。
- [x] 初始化失败、GPIO 错误及显式业务故障仍可停喂，停止后不可恢复。
- [x] 通过现有 GPIO 与采集/status 公开边界测试，更新旧断言，保持当前固件可构建。

## Comments

用户已确认按 01 → 02 → 03 顺序实施；依据本功能正式规格。

## Answer

零登记路径已完成。两个行为红测先复现旧约束；修改后 io 8 项、acquisition 17 项通过。日志位于工作区 build/IDU-pre/task-watchdog/01-*。
