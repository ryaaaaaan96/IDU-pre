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
- 日志：`build/IDU-pre/architecture-review/{budget-*,*-budget-*,assembly-*}.log`。所有构建位于 `build/IDU-pre/architecture-hardening/`，均为 native_sim/native/64；后续故障收敛与固件验收见下节。

## 故障策略、独立指示与完整装配

- `tests/runtime` 编译真实 `app/main.c`、acquisition、协议 `init.c/tasks.c/ports.c`、status、DP/CO/TR；只在采样、LED、UART、Flash 及复位硬件边界注入行为。
- 20 个新进程场景通过：常规复位 6 项、正常留驻 6 项、调试留驻 5 项、LED 继续 1 项、部分协议启动失败 1 项、存储/远端降级 1 项。覆盖采样、发布、LED、维护、输入初始化及显式登记任务超时。留驻检查采样和报文停止、DP 拒绝旧缓存读取，正常外狗持续、调试外狗关闭。
- 部分协议启动失败采用无效 UART 波特率，故障发生于任务已创建但尚未放行阶段；维护故障使用真实 `modbus_disable(RAW_1)`；存储故障在 Flash 打开边界注入 `-EIO`，远端模拟不应答。存储失败和远端离线不触发本地终止。
- 新 UART 夹具最初未消耗发送缓冲而卡住；GDB 确认停在模拟 UART 工作循环，补齐物理 TX 消耗行为后通过，不修改上游驱动。存储挂载在空 Flash 上不一定擦除，改在 Flash 打开边界注入故障，避免无效的擦除注入。
- `shared_rs485` 最终 9/9：真实任务装配、共线缓存读、同步写、两台设备、异常归属释放、真实周期任务，以及终止后公开模块入口和北向请求受阻。新增北向发送占线场景断言完整南向等待 490–520 ms（标称 500 ms 加 native tick 余量），不发送、返回 `-EBUSY`，保持远端状态，释放后读写继续。
- `failures` 是既有“所有调用错误”诊断计数，不是无回复计数；竞争测试按错误类别、是否实际发帧和远端状态验收，状态机本地忙回归继续覆盖不判掉线。
- `acquisition` 最终 19/19：350 ms 同步采样阻塞期间 LED 仍翻转，另验证系统工作队列延迟后跳过错过的边沿、故障常亮优先，以及错误来源、首错保持与降级；独立采样 LED 用例先失败后通过。已有 `task_watchdog` 12 个新进程场景通过，新增运行测试进一步验证任务超时留驻。
- `iap` 新增两个独立进程场景通过：停止先于升级提交则不写 MCUboot 标记并解除冻结；停止发生于已准入提交则完成原提交、保留 TEST 标记，超过原 500 ms 延迟仍不自动复位，后续新路由静默。既有签名、路由、Flash 故障和重启 journal 回归通过。
- 专项日志：`build/IDU-pre/architecture-review/shared-final-{build,test}.log`、`acquisition-final-{build,test}.log`、`iap-stop-{before,during}.log`。主入口包含新的 runtime 和 IAP 场景。

## 固件配置

从工作区运行，均设置 `CCACHE_DISABLE=1`：

- `./applications/IDU-pre/scripts/build.sh`：MCUboot + IAPv3，通过构建、签名验证、SGZ 导出。
- 同命令增加 `--extra-conf /home/ryan/Documents/zephyrproject/applications/IDU-pre/conf/debug.conf`：显式调试版本通过。脚本切换至工作区，额外配置使用绝对路径；应用目录示例见 `docs/fault-policy.md`。
- 同命令增加 `--no-upload --pristine always`、`--smp-uart --pristine always`：关闭升级入口及 SMP 两种配置均通过构建、签名验证。no-upload 旧缓存含已删除的 `IDU_AIDC_PROFILE`，仅清理对应生成目录后构建成功；未改依赖或签名密钥。
- 最后重新构建默认 IAPv3，当前生成配置确认 `APP_DEBUG_SKIP_WATCHDOG` 未启用、交换预算 500 ms、响应 100 ms。
- 日志：`build/IDU-pre/architecture-review/firmware-{normal,debug,no-upload,smp}.log`；固件在 `build/hex/`，不入库。

## 回归执行记录

主入口：从工作区运行 `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`。
原 acquisition 构建缓存还指向 Zephyr 默认 Kconfig，缺失新增策略符号；测试 CMake 显式指定自身 Kconfig 后，使用同一脚本剩余命令从 acquisition 续跑。日志分别为 `full-hardening.log` 与 `full-hardening-remainder.log`；最终新增共线竞争场景另在 `shared-final-test.log` 验收。续跑成功，完整脚本覆盖的主机 C、23 项 Python 测试及所有 native 套件完成；最终 LED 补充场景在 `acquisition-final-test.log` 通过。此前已通过的未变化套件不重复计为新增覆盖。

Arm 默认应用链接用量：Flash 149856 B，RAM 119932 B / 128 KiB（91.50%）；RAM 包含预留系统堆与线程栈，不代表实测堆峰值或栈高水位。

## 实物边界

未连接实板，尚未测量 RS485 电气波形及满负载竞争、LED 最大抖动、外部看门狗禁用/持续喂狗/复位链、真实并发堆峰值和栈高水位。工作区复用、故障注入和 Arm 构建不替代这些结果。测试使用 `ASAN_OPTIONS=detect_leaks=0`，不将通过解释为泄漏验证。
