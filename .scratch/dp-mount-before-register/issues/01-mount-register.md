Status: resolved
Blocked by: None

# 验证

dp_init 先挂载分区并恢复存储事务，protocol_init 挂载成功后登记产品表。
先做基本入参、大小和重复键检查，再分配整表内存；逐点循环完成配置校验、
初值检查及历史值加载，缺失参数批量提交，全部成功才发布。
删除独立 dp_load_value、延迟挂载转发和重复参数计数。
错误回退本表目录并释放节点；存储提交失败冻结写入，历史类型不兼容不清空其他表。

验证：DP 和 process native_sim 通过；挂载申请失败可重试、未初始化拒绝登记、
登记返回即恢复历史值、挂载后继续登记、新点默认值、SKIP_RESTORE 上电恢复通过。
512 行重复键、容量不足回退后继续登记、类型不兼容回退且原参数不变、
登记提交故障不发布表、读写回调存储故障通过。
旧二进制生成的 NVS 文件由新代码读取并恢复成功，存储格式保持兼容。
默认 MCUboot + IAPv3 构建及验签通过；Flash 91,376 B，静态 RAM 41,576 B。
测试入口 tests/run.sh 已加入新故障与跨进程场景，bash -n 通过；未跑完整回归或实板测试。
日志 /tmp/eager-dp-{build,test}.log、/tmp/eager-process-{build,test}.log、
/tmp/eager-{register-failure,read-failure,main,incompatible,verify,compat}.log、
/tmp/eager-firmware.log。
保留用户原有首个 ISR 判断格式和 initial 行末空格两个未暂存修改块。
