Status: resolved
Blocked by: None

# 单回调整理

status 通过 watchdog_init(status_watchdog_fault) 绑定唯一私有回调，main 无需新增入口。
status.c 按状态维护与查询、故障记录与产品动作、回调与初始化、镜像确认分段。
CONTINUE 提前返回，终止路径集中停止业务、记录日志和请求复位。
保留锁内停喂及状态锁存，watchdog 在 LED 前初始化，未增加公共接口。

## Answer

- tests/task_watchdog/run_modes.sh 通过：四种配置、初始化失败、首次检查宽限、25/200 ms 周期，共 50 个独立进程场景。
- acquisition/run.py 通过：39 个独立进程场景。
- MCUboot + IAPv3 固件构建及签名验证通过；产物位于工作区 build/hex/IDU-pre-mcuboot-iapv3/。
- git diff --check 通过；未进行实板验证。
