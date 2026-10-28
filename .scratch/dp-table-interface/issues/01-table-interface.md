Status: resolved
Blocked by: None

# 点表登记与同步读写

验收：四个主接口及调用迁移完成；无 dp_stop/dp_stopped；删除空参数操作；
DP 核心、跨进程持久化、产品装配、ADC 和看门狗相关验证通过；固件构建通过。

## 验证

2026-10-28：

- native_sim DP 核心 1/1：分区登记、表登记顺序、重复 ID、范围、命令、状态读取及设备隔离。
- 同一模拟 Flash 两次进程执行 main / verify：参数保存、重启后读取、分设备恢复默认通过。
- native_sim process_runtime 1/1：产品登记、本地 DI 和采样周期读写、参数状态通过。
- native_sim ADC：应用 6/6、驱动 8/8 通过。
- 看门狗 25 个独立进程场景通过，终止状态由 status 锁存，不依赖 DP 停止标志。
- 默认 MCUboot + IAPv3 构建、签名验证、HEX/BIN/SGZ 导出通过。
- git diff --check 通过。

本轮未进行实板验证。NVS 格式保持不变；IAPv3 无路由，仍不能宣称串口上传可用。
