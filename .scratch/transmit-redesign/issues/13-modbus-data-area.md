Status: resolved
Blocked by: None
Type: task

# 统一 Modbus 数据区命名

## 目标与验收

按用户确认将 mb_object_type_t 改为 mb_data_area_t，MB_OBJECT_* 改为 MB_DATA_AREA_*，
请求及转发表的 object_type 改为 area。更新应用、模块、测试和当前说明，
保留既有历史任务记录；数据区取值、默认权限与运行行为不变。本轮不提交。

## 验证

- transmit_stream：18/18 通过。
- protocol_port：1/1 通过。
- modbus_support-server：两个套件共 6/6 通过。
- 当前模块、应用、测试和设计文档无旧标识符残留；git diff --check 通过。
- 未执行实板测试，未提交。

## Answer

统一为 mb_data_area_t、MB_DATA_AREA_*、area，枚举顺序及取值保持不变。
Zephyr v4.4.2 的本地 modbus.h 使用分区回调，未提供统一数据区枚举；
本项目的枚举属于自有适配层。已同步更新模块说明。
