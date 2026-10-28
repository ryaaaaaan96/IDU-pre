Status: resolved
Blocked by: None

# 回调上下文

统一读写回调上下文；表内索引直接取查找下标，没有额外索引表或常驻字段。
读回调获得当前快照副本，可修改；成功且校验通过后提交。
写命令获得可修改输入副本，保留不缓存触发值的语义。

验证：dp_core、process_runtime native_sim 测试通过，默认 MCUboot + IAPv3 构建及验签通过。
测试检查设备/键/行下标、命令输入可修改但不缓存、连续读回调修改当前快照、失败与越界不提交。
日志：/tmp/context-dp-{build,test}.log、/tmp/context-process-{build,test}.log、
/tmp/context-firmware.log。未进行实板验证。
用户原有 points.c 首个 ISR 判断格式及 points.h initial 行末空格保持未暂存。
