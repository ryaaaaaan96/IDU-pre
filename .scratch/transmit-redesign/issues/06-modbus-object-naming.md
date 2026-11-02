# Modbus 数据对象命名

Status: resolved
Blocked by: None

## 目标与结果

按本轮重命名要求，将 mb_area_t 改为 mb_object_type_t，area 成员及内部参数改为
object_type，四个枚举常量统一为 MB_OBJECT_*。同步 Modbus 请求、转发表、应用与测试。
本轮只修改命名，保留现有 tr_access_t 权限行为；权限简化属于后续行为调整。

## 验证

重新编译并运行 native_sim：transmit_stream 16/16、protocol_port 1/1、
modbus_support-server 6/6 通过；旧类型及枚举名称无残留，git diff --check 通过。
未进行实板验证。按用户此前要求不提交。
