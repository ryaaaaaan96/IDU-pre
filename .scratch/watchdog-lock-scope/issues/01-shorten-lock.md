Status: resolved
Blocked by: None

# 缩短 check_tasks 的临界区

验收：锁不覆盖故障模式判断、回调和工作调度；链表和诊断交接一致性保留；外部喂狗开关矩阵、超时/健康过期/延时复位验证通过，产品构建通过。

## Answer

已将 check_tasks 的整段持锁改为任务检查和逐条诊断交接的独立临界区。模式判断、回调、周期调度在锁外；任务心跳和永久休眠检测到超时时，也先释放任务锁再调度通知。保留出队与诊断更新的原子交接，未新增状态或公开接口。

看门狗矩阵 58 个独立模拟器场景通过，覆盖外狗开/关各 25 个场景、初始化失败、首次检查过期及 25/200 ms 检查周期。日志位于工作区 build/watchdog-*.log。实板未验证；未暂存、未提交。

MCUboot + IAPv3 固件构建、签名验证及 SGZ 导出通过，日志为 build/watchdog-lock-firmware.log。首次构建因默认 ccache 目录只读失败，改用 /tmp/idu-ccache 后通过。git diff --check 通过。
