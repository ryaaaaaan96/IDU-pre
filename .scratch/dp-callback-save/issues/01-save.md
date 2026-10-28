Status: resolved
Blocked by: None

# 验证

读写回调后统一校验和保存，持久读先恢复当前值；允许持久参数使用回调。
恢复命令成功后应用清零。SKIP_RESTORE 只跳过显式恢复默认，上电仍恢复历史值。
配置检查与初值展开、初值校验在登记流程分开表达。

DP 和 process 模拟测试通过，覆盖回调修正输入、最终越界、回调失败、读写 Flash 失败、
同值去重、命令清零和跨进程恢复；SKIP_RESTORE 持久点上电恢复且显式恢复默认保留。
默认 MCUboot + IAPv3 构建及验签通过，Flash 91,660 B、RAM 41,576 B。
日志 /tmp/callback-store-dp-{build,test}.log、/tmp/callback-store-read-failure.log、
/tmp/callback-store-{main,verify}.log、/tmp/callback-store-process-{build,test}.log、
/tmp/callback-store-firmware.log。未进行实板测试。
用户原有首个 ISR 判断格式和 initial 行末空格保持未暂存。
