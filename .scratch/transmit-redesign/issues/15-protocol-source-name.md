Status: resolved
Blocked by: None

# 协议装配入口命名

将 app/protocol/process_runtime.c 改为 protocol.c，与 protocol_init() 及公开头文件命名一致。
该文件负责 DP 初始化、本地点表登记和可选通信端口初始化。
同步产品与测试 CMake 路径、当前模块说明和表内注释，不修改历史验证记录及测试套件名。
函数、接口和运行行为保持不变。

## 验收

process_runtime 与 protocol_port 模拟器测试通过，差异检查通过。

## 验证结果

process_runtime 1/1、protocol_port 1/1 通过；git diff --check 通过。
仅重命名源文件，未改业务逻辑；未重复进行实板验证。
