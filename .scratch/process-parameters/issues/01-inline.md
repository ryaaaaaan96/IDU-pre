Status: resolved
Blocked by: None

# 默认值、范围和恢复策略归处理点表

按[已确认方案](../spec.md)合并点配置，删除 IDU 平行参数数组和查表桥接。
恢复及范围开关按用户后续要求统一放入 sig_flag。

## Answer

DM_SIG_CFG_S 内嵌 data；FLAG_NO_RESTORE 禁止主动恢复默认，FLAG_RANGE_CHECK
开启上下限检查，双零可表示只允许零。默认值解释、国家查询及初始化前的固定选择归 DP。
旧模块消费者的回调适配保留，IDU 使用行内配置；原持久参数与回调业务行为保留。
47 次 native_sim、310 项 Ztest、主机回归及 IDU/IAPv3 构建验签通过，
详见[验证记录](../../../docs/testing.md)。未进行实板验证。
