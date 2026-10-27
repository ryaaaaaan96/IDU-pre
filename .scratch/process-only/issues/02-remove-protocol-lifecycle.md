Status: resolved
Blocked by: 01

# 删除当前 protocol 兼容故障接口

删除 process_runtime.c 的 protocol_error/protocol_stop/protocol_stopped、原子停止标志和初始化判断。
main 删除恒成功的维护查询；status 直接调用 dp_stop，不增加转调接口。
旧通信实现和声明仅供保留源码及其回归测试使用，不属于当前运行入口。

## 验证

- process_runtime native：1/1 通过。
- task_watchdog native：25/25 独立进程场景通过，故障回调直接停止 DP 的模拟入口。
- acquisition：本轮相关 6 个独立场景通过（成功启动、采样失败、写点失败、故障动作、输入初始化失败、终止后不恢复）。
- acquisition 完整运行在 test_fault_during_gpio_write_forces_indicator_on 的 LED 图案断言失败；未修改 LED，未宣称整套通过。
- 固件构建、签名验证和导出通过；无实板验证。
- git diff --check 通过。
