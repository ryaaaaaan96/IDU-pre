# 使用 Modbus 对象默认权限

Status: resolved
Blocked by: None

删除 tr_access_t、转发表 access 成员及相关预检和配置校验。
协议功能码决定对象类别，线圈与保持寄存器读写，离散输入与输入寄存器只读。
保留地址覆盖、整点写入及 DP 回调和范围校验；同步当前设计文档和规格。

验证：transmit_stream 16/16、protocol_port 1/1 模拟器测试通过。
补充验证写功能码不能访问仅登记在离散输入/输入寄存器区的地址，并检查位图未改变。
git diff --check 通过。未进行实板验证，按用户要求未提交。
