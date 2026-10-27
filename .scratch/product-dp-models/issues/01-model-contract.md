Status: resolved
Blocked by: None

# 模型配置接口与产品装配分离

实现模型登记与多模型只读配置，迁移 product binding，删除已被替代的设备登记接口。

## 验证

- dp_models：1/1，通过共享定义、独立实时值、16 位设备 ID、范围拒绝、重复设备/空模型/通信标志拒绝；关闭 DP_REFERENCE_TABLES、不启用 CO/TR、不启动 DP 线程。
- process_runtime：1/1，当前产品模型初始化、默认值查询、同步写点及停止通过。
- table_registry：10/10，产品装配迁入 protocol 后通过。
- forwarding_devices：6 通过、1 跳过（需指定重启阶段的持久迁移场景），未重新启用产品通信。
- reference_config：2/2，未迁移的参考模块调用方仍可运行。
- 标准固件构建、MCUboot 签名校验和 HEX/SGZ 导出通过。
- git diff --check 通过；未进行实板测试。

## Answer

当前产品不再依赖 DEV_TABLE_S 设备目录、getter 回调或产品采集配置来创建 DP 模型。
DP 通过数据模型登记并初始化处理视图；产品设备组合由 protocol 负责。
参考协议解析等未涉及实现没有全量重写，暂时停用的通信将按新模型契约再接入。
