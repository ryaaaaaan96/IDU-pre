# 两项 P2 修复验证

基线：`f4fa6ee`。仅修改 applications/IDU-pre，不纳入用户 protocol.c getter 格式修改。

## 存储故障独立记录

- Red：完整北向非持久写成功后，存储状态无法保留 ENOENT；应用成功操作后 storage_error 被清零。
- Green：`dp_param_status.storage_error` 独立表达存储故障，`error` 仍是最近操作结果；所有状态由原缓存锁保护，不改变 NVS 或线协议。
- collect_state-storage-fault 14 项通过；acquisition 17 项通过，包含故障持续、继续采样/喂狗、不能确认镜像，以及存储恢复后解除降级。
- points 的真实 Flash 写故障、成功 NO_STORE 更新、存储 reload 清故障用例通过。

## IAPv3 站号 247

- Red：产品配置允许 IAPv3+247，新增配置拒绝测试失败。
- Green：在产品 CMake 明确拒绝该组合；关闭 IAPv3 时 247 以及开启时 246/16 正常。
- test_build_configuration.py 8 项通过；IAP 接收器原协议过滤和状态寄存器布局未改。

## 最终验证

完整回归、实际错误配置拒绝、默认 IDU/MCUboot/IAPv3 构建及两轴审查进行中，待结果归档。
