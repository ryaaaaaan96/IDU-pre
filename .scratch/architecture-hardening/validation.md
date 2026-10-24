# 架构修复验证

仅记录本轮修改后的结果。实板条件不可用，RS485 波形、LED 抖动、外狗和复位、堆峰值与栈高水位仍待实测。

## 本地样本工作区

- `tests/points` 新增本地堆压力测试，修改前返回 `-ENOMEM` 导致断言失败，修改后通过。
- 首次工作区申请失败释放所有权；重试成功。后续本线程堆分配被拒绝时，正常与失效样本均可交付。
- native_sim/native/64 的 points 53 项通过，1 项重启专用测试按条件跳过，包括质量、队列拥塞与恢复、参数事务。
- 构建：`CCACHE_DISABLE=1 ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/points -d build/IDU-pre/architecture-hardening/points`。
- 运行：`ASAN_OPTIONS=detect_leaks=0 timeout 30s build/IDU-pre/architecture-hardening/points/zephyr/zephyr.exe -flash_in_ram`。
- 日志：`build/IDU-pre/architecture-review/points-{hardening-build,pressure-red,pressure-green}.log`。禁用泄漏扫描，未据此宣称无泄漏。
