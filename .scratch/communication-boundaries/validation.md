# 协议编排重构验证记录

实施审查基线：`a51ec3918151d078a607654cdad3b61fb77b80b6`。
本地已有的 `protocol.c` 设备数量 getter 格式修改不属于本任务，不纳入提交。
使用用户批准任务中约定的完整报文、DP 公共读取及公开状态接口，不新增私有测试入口。
测试构建禁用 ccache，避免默认缓存目录只读；构建产物均在工作区 build 内。

## 01：显式模块连接

- Red：pipeline 在未注册消费者时执行采集，期望 `-ENOTCONN`；旧实现隐式发布到 DP，断言失败。
- Green：显式注册消费者后，pipeline 6 项通过，包含样本复制、完成等待、初始化及北向报文读取。
- collector 5 项通过；points 46 项通过、1 个独立重启探针正常跳过；shared_rs485 7 项通过；acquisition 13 项通过。
- collector 无适配器 5 项通过；pipeline 无适配器 6 项通过。
- 未执行实物验证。完整回归及固件验签留到最终集成任务，不在每轮重复运行。

## 02：应用创建 DP 任务

- Red：pipeline 在应用未放行消费者时等待样本完成，旧静态线程已经处理，预期超时断言失败。
- Green：不创建模块线程后，入队样本保持待处理；测试应用启动两个消费者后票号正常完成，pipeline 6 项通过。
- dataprocess 9 项、reference_services（参数配置）11 项、reference_dp（关闭业务处理）3 项、pipeline_basic 3 项通过。
- points 集成 46 项通过、1 个独立重启探针正常跳过，覆盖处理回调、参数、定时器和多总线。
- 产品协议层创建挂起的 DP 执行上下文，并显式放行；后续 05 再统一收敛所有协议任务的启动位置。

## 03：应用创建 CO 任务

- Red：collector 在启用总线、应用尚未启动工作线程时提交请求；旧模块线程提前完成，预期等待超时的断言失败。
- Green：`co_start()` 不创建线程，应用启动 `co_bus_task()` 后请求正常完成，collector 5 项通过。
- points 46 项通过、1 个正常跳过；shared_rs485 7 项、reference_ports 11 项、collector 无适配器 5 项通过。
- 模块保留总线状态和队列；产品仅声明当前总线的线程和栈，配置容量不再隐式分配模块线程栈。

## 04：北向服务独立运行

- Red：transmit 在未初始化时调用维护，旧服务隐式启动，预期 `-ENODEV` 的断言失败。
- Green：显式初始化、绑定应用队列后，transmit 9 项通过，覆盖启动期收包、动态重配置及失败保留原写入器。
- Red：GPIO 地址变化后向南向读应成功，旧代码却用 GPIO 点判断北向冲突而拒绝；修正为初始化配置站号后通过。
- shared_rs485 7 项、acquisition 13 项、forwarding_devices 6 项（重启探针正常跳过）、communication 大报文 11 项通过。
- points 46 项通过、1 个正常跳过。北向站号与维护间隔由 .conf 显式设为 16 和 100 ms，南向站号不变。
- 模块不再声明/启动接收和重配置队列，应用独立维护；仅在本地采样健康检查时读取已锁存的维护故障。

## 05：统一本地协议装配

- Red：转发初始化失败时，旧顺序已放行 DP/CO；新增启动失败测试捕获到提前放行。
- Green：统一 `idu_protocol_init()` 完成配置、注册、参数恢复和转发初始化后才放行 DP/CO，acquisition 14 项通过。
- IAP 初始化在统一协议入口之前完成，开放升级路由不依赖尚未就绪的升级接收器。
- points 46 项通过、1 个正常跳过；shared_rs485 7 项、table_registry 10 项通过。
- 产品初始化实现只加入实际 IDU 应用；独立模块测试继续使用自身公开接口和显式上下文，不引入产品设备树依赖。

## 06：初始化标记与连续组包

- Red：collect_state 期望连续初始化寄存器合成一包，旧实现每行发送一次，首包数量断言失败。
- Green：连续合并、32 寄存器容量拆分、两段 16 字测点值、地址空洞和不同设备隔离，collect_state 2 项通过。
- reference_configuration 2 项通过，覆盖未标记零周期不产生初始化读、显式标记及 60000/65535 毫秒周期。
- points 46 项通过、1 个正常跳过；shared_rs485 7 项通过。产品协议号/地址读已显式标记，未改变参数键或点值池。
- 请求和映射字段在有界任务栈上临时组包，不为每个静态点表行分配常驻 RAM 请求副本。

## 07：每设备四状态

- Red：显式写超时被旧实现标为掉线；Green：仅记录结果，不驱动自动轮询状态，也不重试。
- collect_state 11 项通过：初始化、额外重试、异常宽限、掉线降频、首包探测、重新初始化、短暂恢复、有效异常、本地忙和不同设备策略。
- Red：完整 ADU 的畸形载荷被仅检查头部的接口当作成功；Green：集中校验载荷和写回显，单笔只记一次最终结果。
- Red：自动初始化在持总线锁时等待 DP 处理，SAMPLE 回调南向回读形成锁循环，恢复测试超时；Green：物理事务锁与等待业务完成分开，恢复及回读通过。
- collector 5 项、points 46 项（1 个正常跳过）、reference_ports 11 项、collect_modbus 2 项、shared_rs485 7 项通过。
- collector 无适配器 5 项、acquisition 14 项通过。产品手动配置额外重试 2 次、异常最长 3000 ms、掉线探测 5000 ms。
- 自动上线仅读取已配置初始化组；显式设备初始化的业务后处理保留，不因自动发现重放已保存的下发操作。

## 08：失效、逐点恢复与 RAM 诊断

- Red：只初始化读取的缓存点在设备掉线后仍为 GOOD；Green：显式 offline 回调统一置通信失效并保值，排队旧样本不能恢复质量。
- collect_state 13 项通过：完整北向异常/正常响应、其他设备和本地有效、逐点恢复、历史错误、并发查询/清理不改变调度或发送。
- reference_services-params 12 项通过：保存目标 42、采集反馈 99、掉线失效、无关本地写后仍失效、重新加载与显式同步仍为 42。
- 参数提交限定为本次选中项，避免写其他参数时批量恢复失效质量；没有改变 NVS 格式或保存目标。
- acquisition 15 项、shared_rs485 7 项通过。应用状态新增远端掉线/初始化数量，远端故障仍正常采样和喂狗。

## 09：存储故障降级与 IAP 保护

- collect_state-storage-fault 14 项通过：真实参数模块打开不存在的分区返回 ENOENT；同一装配仍处理本地采样、南向周期采集和北向完整请求。
- 北向持久写失败，读回仍为默认值 25；直接持久写与 prepare_reboot 均返回 EROFS，未假报保存成功。
- acquisition 15 项通过，存储错误进入应用诊断，仍采样/喂狗，不能确认试运行镜像。
- IAP 的实际接收流程在 prepare_reboot 返回 EROFS 后进入错误态，不提交 MCUboot 请求、不重启；既有 boot request 错误测试继续通过。
- parameter_records 11 个独立进程阶段全部通过：write/add/remove/readd/collision/verify/seed_error/range_change/type_change/width_change/obsolete。
- 没有改变 NVS 键、记录版本、意图日志、参数区或 IAPv3 线协议；存储降级不等于允许绕过升级保存保护。

## 10：集成、审查与固件验收

- 统一装配和执行上下文已收敛到 app/protocol/init.c、tasks.c；main 调用一个协议初始化入口。
- DP/CO/TR 模块内已无 K_THREAD_DEFINE、k_thread_create 或 k_work_queue_start；相关等待、队列和算法仍归模块。Zephyr 框架线程及 IAP 任务归属未改。
- 两轴代码审查初审各发现 1 项，均已修复并由原代理复核通过；详见 [审查记录](review.md)。
- Red：同线南向站号等于北向站号仍成功启动；Green：开放端口和准备任务前返回 EADDRINUSE，acquisition 16 项通过。
- 同一 collect_state 存储故障装配 14 项通过，北向持久写为 Server Device Failure；北向非持久命令实际发送一次，设备掉线恢复不重放。
- 固件命令：`CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh`；IDU/MCUboot/IAPv3 构建、两次镜像验签与 SGZ 导出成功。
- 产物：工作区 `build/hex/IDU-pre-mcuboot-iapv3/` 的 factory/app/boot HEX、update BIN/SGZ、公开密钥及 validation.json；签名私钥继续保存在 build/keys，未提交。
- validation.json：signature_verified=true、hardware_verified=false、iap_sgz_supported=true；签名应用 129662 B，槽位 458752 B。

### 资源

基线为本轮开始前已完成的独立转发入口构建，见 [前轮验证](../forwarding-devices/validation.md)。

| 指标 | 迁移前 | 本轮 | 变化 |
| --- | ---: | ---: | ---: |
| IDU Flash | 125240 B | 129512 B | +4272 B |
| IDU 静态 RAM | 102940 B | 104124 B | +1184 B |
| MCUboot Flash | 36568 B | 36568 B | 0 |
| MCUboot RAM | 18096 B | 18096 B | 0 |

应用实际创建 DP 样本 4096 B、DP 业务 6144 B、CO 总线 4096 B、TR 接收 4096 B、TR 配置 2048 B，
相关 5 个执行上下文共 20480 B 声明栈，与原总容量相同；所有权迁移没有宣称节省栈。
CO 状态与诊断增加 RAM；链接器 RAM 占用约 79.44%。以上是链接占用，不是运行栈高水位或峰值堆占用。

### 验证边界

- 主机：ASan/UBSan C 及 Python 回归；当前 ptrace 环境不支持 LeakSanitizer，按已有测试说明仅关闭 detect_leaks，保留其余地址/未定义行为检测。
- native：完整脚本覆盖现用模块与关闭适配器/业务处理、启用参数等变体，独立进程参数记录和 IAP 日志覆盖软件重启边界。
- 首轮回归捕获新增夹具未注册写回调（实际返回 Illegal Function）；补齐实际 tr_write 连接后，相关测试通过；修复后重新执行完整脚本，单次退出 0。
- 实物待验证：共享 RS485 的外部主站窗口、DE/帧时序、吞吐、长时间故障恢复、任务栈高水位、NOR 断电中断写及整机升级。
- 未改上游仓库、Flash 分区、私钥、参数键/记录版本或 IAPv3 线协议；不宣称外部多主仲裁能力。

### 最终完整回归

工作区执行 `ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh`，退出 0。
完整日志在 `build/IDU-pre/communication-validation/regression.log`，固件日志在同目录 `firmware.log`，均为本地构建产物不提交。
所有实际请求、质量/参数缓存、掉线恢复、诊断清理、存储失效及独立进程重启验证通过。
代码变更相对实施基线的 diff --check 通过；用户 getter 原有的未提交格式改动仍保留。
