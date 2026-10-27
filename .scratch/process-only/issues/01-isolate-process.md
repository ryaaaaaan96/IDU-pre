Status: resolved
Blocked by: None

# 隔离 process 运行入口

新增 `process_runtime.c`，产品不编译旧通信装配，原通信源码和测试配置保留。
仅绑定本地 process，保留 DP 样本和业务线程；本地采样、状态和看门狗原流程不改。
不挂载原参数区、不恢复南向事务，使用 RAM 默认值。

## 验证

- native_sim：`tests/process_runtime` 1/1 通过，覆盖初始化、仅本地设备、无采集关系、DI 写入读取、远端点不存在、参数存储未就绪、幂等停止和停止后拒绝操作。
- 固件：标准 build.sh 构建 MCUboot + IAPv3，通过签名验证并导出 HEX/SGZ。
- 产品 compile_commands.json 不包含原 protocol/init/tasks/ports.c，也不包含 collect/transmit/segments 点表。
- `git diff --check` 通过。
- 未执行实板验证；当前北向 Modbus、南向轮询、IAPv3 串口接收及参数持久化均暂不运行。

## Answer

本轮只隔离运行装配，没有重写通用 DP/CO/TR。下一步在独立入口下逐项审查 process 回调、事务和定时处理。
