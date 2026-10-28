Status: resolved
Blocked by: None

# 验证

DP 核心和 process 装配 native_sim 测试通过。
WORD/BYTES/STRING 范围标志以及非持久跳过恢复标志打印携带设备和键的警告，
登记及读写成功；未启用的无效上下限静默忽略。
错误长度、零长度、奇数 WORDS、越界初值及颠倒数值范围仍拒绝登记。
默认 MCUboot + IAPv3 构建、签名校验通过；未执行实板验证。
日志 /tmp/warnings-dp-{build,test}.log、/tmp/warnings-process-{build,test}.log、
/tmp/warnings-firmware.log。保留用户已有两个未暂存修改块。
