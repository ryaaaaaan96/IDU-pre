Status: resolved
Blocked by: None

# 验证

number 函数删除，check_value 用 switch 和 DP_OUTSIDE_RANGE 文件内宏直接检查相应成员。
保留 U8 高字节检查、非数值尾字节检查及范围开关；数值无需转换成 int64_t。
DP native_sim 通过，包括 U8/U16/S16/S32，补充 U32 跨 INT32_MAX 的合法上界和越界检查。
既有回调失败、最终值校验、并发快照及恢复默认测试通过。
默认 MCUboot + IAPv3 构建及验签通过，未进行实板验证。
日志 /tmp/range-dp-{build,test}.log、/tmp/range-firmware.log。
用户原有两个未暂存修改块保留。
