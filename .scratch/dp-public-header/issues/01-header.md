Status: resolved
Blocked by: None

# 验证

对外头改为 dataprocess/dataprocess.h，保护宏 DATAPROCESS_H，调用和文档同步。
DP 和 process 模拟测试通过，默认 MCUboot + IAPv3 构建及验签通过，镜像摘要未变化。
日志 /tmp/header-dp-{build,test}.log、/tmp/header-process-{build,test}.log、
/tmp/header-firmware.log。未进行实板验证。
原用户 initial 行末空格随头文件迁移保留，首个 ISR 判断格式保持未暂存。
