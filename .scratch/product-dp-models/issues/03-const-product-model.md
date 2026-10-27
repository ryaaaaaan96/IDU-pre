Status: resolved
Blocked by: 02

# 编译期只读产品模型

在测点表所属编译单元定义 const 模型描述，ARRAY_SIZE 自动计算点数；运行入口直接登记。
删除 data_device_cfg、可写模型数组、运行时查表生成及应用侧 dp_config 判断。
重复登记/初始化由 DP 拒绝，不改变运行时测点存储。

## 验证

- process_runtime native：1/1，验证重复登记返回 EBUSY，模型指针不变，同步读写及停止正常。
- 固件构建、MCUboot 签名验证及 HEX/SGZ 导出通过。
- nm 检查：protocol_local_model 为 R、local_process_rows 为 r，均位于固件只读段。
- git diff --check 通过；未实板验证。

## Answer

模型描述和测点数量均在编译期固定，无运行时模型写入；删除应用侧初始化保护，由 DP 统一拒绝重复登记。
