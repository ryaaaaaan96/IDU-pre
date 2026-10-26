# 最近故障记录

按用户要求，status_report_fault 每次更新描述、阶段、错误、时间和动作并输出日志。
删除 first、upgrade 及 terminal_description。动作通过显式 switch 执行，重复终止
请求仍执行，业务阶段独立锁存，后续 CONTINUE 不恢复业务。

测试覆盖继续、重复留驻、重复复位和终止后继续上报，最近故障覆盖之前记录。
运行期、初始化留驻、看门狗和停止边界由完整 tests/run.sh 回归验证。
未进行实物验证。

验证结果：完整 bash tests/run.sh 退出码 0；独立采样与状态 39 场景通过，
完整回归包含 runtime 16、task_watchdog 12 场景。git diff --check 通过。
日志位于工作区 build/latest-fault-regression.log 和 build/latest-fault-acquisition.log。
