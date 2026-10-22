# 两项 P2 修复验证

基线：`f4fa6ee`。仅修改 applications/IDU-pre，不纳入用户 protocol.c getter 格式修改。

## 存储故障独立记录

- Red：完整北向非持久写成功后，存储状态无法保留 ENOENT；应用成功操作后 storage_error 被清零。
- Green：`dp_param_status.storage_error` 独立表达存储故障，`error` 仍是最近操作结果；所有状态由原缓存锁保护，不改变 NVS 或线协议。
- collect_state-storage-fault 14 项通过；acquisition 17 项通过，包含故障持续、继续采样/喂狗、不能确认镜像，以及存储恢复后解除降级。
- points 的真实 Flash 写故障、成功 NO_STORE 更新、存储 reload 清故障用例通过。

## IAPv3 站号 247

- Red：产品配置允许 IAPv3+247，新增配置拒绝测试失败。
- Green：在产品 CMake 明确拒绝该组合；关闭 IAPv3 时 247 以及开启时 246/16 正常。
- test_build_configuration.py 8 项通过；IAP 接收器原协议过滤和状态寄存器布局未改。

## 复审补充

- Spec 复审指出：远端持久事务在删除意图记录时的 Flash 错误出口尚未标记存储不可用。
- Red：只对外部 NVS 的意图删除注入 EIO，公开状态无法报告 storage_error。
- Green：该出口清 STORAGE_READY 并锁存错误，保留 PENDING；reload/recover 后恢复且不重放下发，针对性测试通过。
- 故障注入针对原有固定 NVS 记录 ID 2，仅测试夹具使用，不修改存储格式或产品协议。

## 最终验证

- 完整命令：`ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh`，最终版本单次退出 0。
- 65 次 native suite 执行中 371 项通过；Python 6+8+9 共 23 项通过，主机 ASan/UBSan 回归通过。当前环境仅关闭不支持的 LeakSanitizer，编译无 warning/error。
- 实际固件以 `--extra-conf /tmp/idu-p2-iap247.conf` 配置 IAPv3+247，正确触发产品 CMake 错误；默认重新构建成功，最终站号恢复 16。
- 最终 IDU/MCUboot/IAPv3 构建、两次签名验证、BIN/HEX/SGZ 导出成功；validation.json 的 signature_verified=true、hardware_verified=false。
- IDU 链接 Flash 129512 B、RAM 104140 B；RAM 比基线增加 16 B，来自独立诊断状态及消息。MCUboot 为 Flash 36568 B、RAM 18096 B。
- 两轴初审中 Standards 0 项、Spec 发现 1 项错误出口遗漏；补修后两个原审查代理复核均无遗留，见 [review.md](review.md)。
- 完整日志归档到工作区 `build/IDU-pre/review-p2-validation/`，产物及私钥均不提交。没有修改上游仓库、NVS 格式、Flash 分区或 IAPv3 线协议。
- 没有执行实物 RS485、栈高水位及 NOR 断电测试；软件验证不替代实测。保留用户 getter 格式修改，未推送。
