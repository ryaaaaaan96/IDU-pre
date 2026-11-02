Status: resolved
Blocked by: None
Type: task

# 拆分 transmit 公开头文件

## 目标与验收

按映射、Modbus 配置、协议配置拆分公开声明，transmit.h 按依赖顺序汇总，
保留实例配置和登记接口。子头文件可独立包含，不改变类型、接口与运行行为。
保留已有工作区修改，本轮不提交。

## 验证

- 四个公开头文件分别单独包含，使用 transmit 模拟器构建参数执行语法检查，全部通过。
- transmit_stream 重建并运行：18/18 通过。
- protocol_port 重建并运行：1/1 通过。
- git diff --check 通过。
- 本轮只整理声明，未执行实板测试，未创建提交。

## Answer

新增 mapping.h、modbus.h、protocol.h，transmit.h 按该顺序汇总。
子头文件自行包含所需依赖；接口、类型布局和运行逻辑保持不变。
模块 README 已更新公开头文件说明。
