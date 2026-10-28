Status: resolved
Blocked by: None

# 验证

删除 DP 的 params.c/params.h、dp_init、NVS 选择和依赖、参数预算、持久化及跳过恢复标志，
删除三行产品存储维护点及对应枚举、命令/状态回调；保留 21 行 RAM process 点表。
DP 仅保留登记、RAM 写读、范围及读写回调，一把点值锁；初值、时间和失败原子性保持。
IAP DP 保存门禁删除，独立 NVS 日志保留，显式保持默认 NVS_DATA_CRC=y。
原参数分区声明及旧内容未擦除。配置符号删除后所有相关构建 pristine。

验证：dp_core 和 process_runtime 在无 Flash/NVS 配置及无 overlay 下通过；
DP 覆盖分配失败回退、512 行重复、多设备、范围、数组、回调及并发三字节快照。
同一测试程序独立启动两次，均从初值开始；ADC 14 项通过。
IAP configured 模拟测试 8 通过、2 按设计跳过（10 总计）；保留独立日志能力。
默认 MCUboot + IAPv3、--no-upload 构建和验签均通过。
默认 Flash 86,792 B、RAM 37,248 B；无上传 Flash 68,648 B、RAM 18,376 B。
无上传 .config 明确 CONFIG_NVS 未启用，DP 不再依赖 NVS。
未执行实板或完整回归，脚本 bash -n 通过。
日志 /tmp/ram-dp-{build,test}.log、/tmp/ram-restart-test.log、
/tmp/ram-process-{build,test}.log、/tmp/ram-adc-{build,test}.log、
/tmp/ram-iap-{build,test}.log、/tmp/ram-firmware.log、/tmp/ram-no-upload.log。
用户原有两个未暂存修改块保留。
