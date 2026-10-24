# 架构修复验证

仅记录本轮修改后的结果。实板条件不可用，RS485 波形、LED 抖动、外狗和复位、堆峰值与栈高水位仍待实测。

## 本地样本工作区

- `tests/points` 新增本地堆压力测试，修改前返回 `-ENOMEM` 导致断言失败，修改后通过。
- 首次工作区申请失败释放所有权；重试成功。后续本线程堆分配被拒绝时，正常与失效样本均可交付。
- native_sim/native/64 的 points 53 项通过，1 项重启专用测试按条件跳过，包括质量、队列拥塞与恢复、参数事务。
- 构建：`CCACHE_DISABLE=1 ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/points -d build/IDU-pre/architecture-hardening/points`。
- 运行：`ASAN_OPTIONS=detect_leaks=0 timeout 30s build/IDU-pre/architecture-hardening/points/zephyr/zephyr.exe -flash_in_ram`。
- 日志：`build/IDU-pre/architecture-review/points-{hardening-build,pressure-red,pressure-green}.log`。禁用泄漏扫描，未据此宣称无泄漏。

## 交换预算与真实协议装配

- `collect_modbus` 的发送加响应预算测试先失败（570 ms），修复后通过（510 ms，包含 native 的调度粒度）；接口竞争不发送、释放后可继续、显式写丢应答不重试共 4/4 通过。
- `communication` 12/12、`collector` 5/5、关闭 Modbus adapter 的 `collector` 5/5、`points` 53 项通过/1 项重启专用测试跳过。
- `shared_rs485` 已改为调用真实 `protocol_init()`，编入真实 `init.c/tasks.c`，删去测试任务装配。真实任务完成两台设备发现及周期更新，7/7 通过。
- 接入真实发现后暴露测试从机把地址寄存器返回为 `0x55aa`，不在合法范围；修正夹具为真实站号，保留产品校验。
- 日志：`build/IDU-pre/architecture-review/{budget-*,*-budget-*,assembly-*}.log`。所有构建位于 `build/IDU-pre/architecture-hardening/`，均为 native_sim/native/64；故障收敛与固件验收尚待后续完成。
