Status: resolved
Blocked by: None

# 验证

删除 dp_set_initial_value；登记与恢复默认直接复制初值。
参数层 dp_load_value 在挂载后加载兼容历史值，仅无记录时回退初值，错误返回。
移除重复初值循环及历史记录重复编解码，保留原存储格式及不兼容重置策略。
DP、process native_sim 通过；main/verify 跨进程恢复通过。
额外 main/verify 新增持久点测试通过：缺记录点使用初值，已有记录值保持不变。
读回调 Flash 保存失败测试通过。默认 MCUboot + IAPv3 构建及验签通过。
Flash 91,324 B，RAM 41,576 B；未进行实板验证。
日志 /tmp/load-dp-{build,test}.log、/tmp/load-process-{build,test}.log、
/tmp/load-{main,verify,added,read-failure}.log、/tmp/load-firmware.log。
保留用户原有两个未暂存修改块。
