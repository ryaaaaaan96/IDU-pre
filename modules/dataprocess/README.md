# dataprocess

独立 Zephyr 模块，公开接口使用 `dp_*`，负责点值、质量、事务处理和可选的
NVS 参数持久化。把本目录加入 `ZEPHYR_EXTRA_MODULES`，启用
`CONFIG_DATAPROCESS` 即可；不需要注册 collect/transmit，也不需要 Modbus。

`points.h` 保存共享的设备及三类表描述，以便启动时检查键、宽度、地址和
采集归属。运行时不调用采集或转发模块；寄存器编码和 Modbus 回调由
transmit 提供。数据池数值采用主机字节序、低字在前。

## 采样与事务

`dp_init()` 接收静态设备配置。应用线程通过 `dataprocess/points.h` 提交本地遥测：

```c
int dp_set_sig_value(struct dp_key key, const uint16_t *words, size_t word_count,
                     k_timeout_t timeout, uint32_t *ticket);
int dp_set_sig_values(const struct dp_point_update *values, size_t count,
                      k_timeout_t timeout, uint32_t *ticket);
```

目标必须是 `DP_SOURCE_LOCAL`。单点长度为 1…`DP_POINT_WORDS` 个 16 位 word，
匹配点表宽度；批次为 1…`DP_BATCH_POINTS` 项。未知或重复键、宽度及来源错误
在入队前整批拒绝；ISR 调用返回 `-EWOULDBLOCK`。数值按主机字节序、低字在前
传入，长度单位不同于参考平台旧接口的字节数，不承诺旧 ABI 或完成时序兼容。

成功表示输入已复制到有界 zbus 队列，`timeout` 只约束提交等待。
`ticket` 可为 `NULL`；非空时调用开始清零，成功后赋有效票号。
`dp_wait_processed()` 等待处理完成，随后用 `dp_read()` 检查当前值、质量及
错误；完成不等于数值有效。批次可携带采样 `error`，处理时保留旧数值并置
失效质量。换算、范围校验和 SAMPLE 回调由 DP 线程执行。

底层仍使用 `dp_publish()`；collect 通过此入口提交远端样本。硬件读取、
样本到信号键的映射和 IDU 十点快照组批留在应用层。参数写入使用 process/params
接口，事务内输出使用 `dp_txn_emit()`，不通过本地采样接口提交。

`CONFIG_DP_PROCESS` 默认开启，增加 WRITE/READ/INIT/SAMPLE/TIMER 事务、
异步消息队列和专用业务线程。handler 使用 `dp_txn_get()`、`dp_txn_emit()`
等事务访问器，不重入引擎。所有原始输入先准备完成，再执行 handler；
派生输出不递归调用 handler。关闭该配置时，带 handler/read_callback 的
配置会被拒绝，纯值转换和基础数据池仍可工作。

## 声明式初始化和定时器

模块提供初始化机制，由应用明确决定装配顺序。IDU 在 `src/app/startup.c`
保留 `idu_equipment_init()`：先 `dp_init()`，建立采集接口并注册全部总线，
再初始化/恢复参数，由应用判断存储故障是否允许降级，最后调用
`dp_process_start()` 和 `co_start()`。北向转发在首次地址就绪后绑定，
不使用 `SYS_INIT` 隐式决定这组依赖顺序。

`dp_process_plan` 对应参考工程的 INIT/TIMER 注册表思路。回调绑定到测点
handler，使用同一个事务上下文，时间采用 Zephyr `k_timeout_t`：

```c
static const struct dp_key startup[] = {{1, 100}};
static const struct dp_timer_spec timers[] = {
    {.key = {1, 100}, .delay = K_SECONDS(1), .period = K_SECONDS(1)},
};
static const struct dp_process_plan plan = {
    .init_keys = startup, .init_count = ARRAY_SIZE(startup),
    .timers = timers, .timer_count = ARRAY_SIZE(timers),
};
/* 在 dp_init() 和应用完成可选参数恢复策略后，由启动线程调用。 */
int err = dp_process_start(&plan);
```

`initialize_all=true` 可替代显式 INIT 列表，按点表顺序调用全部 handler，
两种选择不能同时使用。定时器的 delay/period 只接受有限相对时间；
`period=K_NO_WAIT` 表示单次。回调在业务线程执行，不在 ISR 中执行，也不承诺
硬实时精度。运行时仍可使用 `dp_process_timer_start/cancel()`。

启动先验证整张计划并预留全部定时器槽，再按序执行 INIT。任一 INIT 失败
都会释放预留槽，计划中的定时器均不会启动，可以重试。此前成功 INIT
事务的业务修改不会撤销。启动成功后重复调用返回 `-EALREADY`。

## 参数存储与下游适配

`CONFIG_DP_PARAMS` 增加 NVS 参数后端，并自动开启事务处理。
`dp_params_init(cfg, area_id)` 不接收串口或 Modbus interface。
只使用本地参数时不需要下游后端。采集模块或应用可在初始化前调用
`dp_params_set_transport()` 注入 read/write/simulated 回调和 user 指针；
注册结构被复制，同一组回调重复注册成功，初始化后替换后端返回 `-EBUSY`。
若同时使用 collect 和自定义参数后端，应先完成全部 `co_init_bus()`，再注册
自定义后端，最后调用 `dp_params_init()`，避免被采集初始化的默认注册覆盖。

回调处理设备键对应的可读绝对设定值。读写可阻塞在所属传输线程上，但不能
重入参数引擎。缺少后端时远端写入在建立持久化意图前返回 `-ENODEV`，
缓存、持久值和本地参数操作不受影响。模拟来源由可选回调报告，默认 false。

远端写前保存意图，丢失 ACK 或重启后只通过回读对账，不重放写入。
本次拆分保留既有 NVS magic、版本、schema 签名和意图字节布局。
关闭参数后端时参数点配置被拒绝，`dp_process_save_reboot()` 返回 `-ENOTSUP`。

`tests/dataprocess` 仅注册此模块，覆盖无 Flash、仅本地 NVS、注入假传输
三种配置，以及 INIT 失败释放槽、初始化顺序和单次启动约束。
