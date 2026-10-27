Status: resolved
Blocked by: 02

# 移除当前入口的 DP 线程

删除 samples/business 线程对象、线程栈及创建/启动代码，保留同步初始化和本地写点。
通用模块与停用通信实现不修改。

## 验证

- process_runtime native：1/1 通过，删除线程后同步写点、读点和停止后拒绝操作正常。
- 固件：标准构建、MCUboot 签名验证及 HEX/SGZ 导出通过。
- git diff --check 通过；未实板验证。
