# dataprocess

[文档导航](../../docs/README.md) · [模块总览](../README.md) · [参考接口](../../docs/reference-platform.md)

独立 Zephyr 模块，公开接口使用 `dp_*`，负责点值、质量、事务处理和可选的
NVS 参数持久化。把本目录加入 `ZEPHYR_EXTRA_MODULES`，启用
`CONFIG_DATAPROCESS` 即可；不需要注册 collect/transmit，也不需要 Modbus。

`points.h` 保存共享的设备及三类表描述，以便启动时检查键、宽度、地址和
采集归属。运行时不调用采集或转发模块；寄存器编码和 Modbus 回调由
transmit 提供。数据池数值采用主机字节序、低字在前。

产品数据模型通过 `<dataprocess/model.h>` 中的
`dp_register_models(const struct dp_model_cfg *models, size_t count)` 一次登记，随后调用 `dp_init()`。
每个模型只有设备 ID、处理测点表和数量，没有串口、Modbus 地址、采集表或转发表。
模型数组和测点定义由应用持有且须常驻，登记后不可修改；共用表的多个模型持有独立实时值。
模型容量由总测点预算约束，不借用采集设备数量限制；设备 ID 为 uint16_t，不限制为通信站号范围。
登记拒绝空模型、重复设备 ID、超量配置、ISR 调用和 FLAG_COLLECT；业务字段仍由初始化校验。
模型初始化不要求启用 `DP_REFERENCE_TABLES`，纯模型测试关闭该选项且不启用 collect/transmit。
模型登记与参考目录回调、原生配置提供者互斥，初始化后不可重新登记。
`dp_register_devices()` 与 `dp_data_device_bind()` 已删除，不提供别名。
产品装配 `data_device_cfg` 和 `protocol_bind_devices()` 归应用 protocol，不再定义于 DP 模块。
参考目录处理暂留供停用通信实现及其测试使用，当前产品不通过该路径初始化。

点值缓冲区在 `dp_init_config()` 中按实际总字数通过 Zephyr `k_malloc()` 一次申请，
随后按偏移供各点使用；服务运行期间不重新分配或释放。初始化失败不会安装配置，
分配失败返回 `-ENOMEM`，申请后初始化失败会释放缓冲区，允许修正条件后重试。
`DP_POINTS_VALUE_WORDS` 是总字数上限，不再对应专用静态数组。
`HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS` 为共享系统堆提供容量预算，模块默认 1024 B，
可在应用 `.conf` 手动调整；堆还包含管理开销，不能将申请字节数等同于固件总 RAM。
测点索引和轮询仍有各自的固定容量；参数缓存、处理事务及大请求缓冲已改为
`k_malloc()`，分别由 `HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS` 和
`HEAP_MEM_POOL_ADD_SIZE_DP_WORK` 贡献容量。详见[内存管理](../../docs/memory-management.md)。

## 采样与事务

业务入口 `data_process.h` 保留 `dp_set_sig_value(dev_id, sig_id, size, bytes)`
的字节长度和同步回调语义；直接读取使用 `dp_get_sig_value()`，需要读回调时
使用 `dp_fast_para_get()`。`platform_callback.h` 提供配置注册，注册完成后
调用无参数 `dp_init()`。支持范围与反汇编证据见
[参考平台对照](../../docs/reference-platform.md)。以下是独立的 Zephyr 扩展接口。

`dp_init_config()` 接收 Zephyr 原生静态设备配置，不创建或启动处理线程。
应用创建线程，以 `dp_samples_task()` 作为样本消费者入口，启用业务处理时另以
`dp_business_task()` 作为业务消费者入口，在本地装配完成后放行。
队列等待和处理算法仍归模块，线程、栈与放行时机归应用。
应用线程通过 `dataprocess/points.h` 提交本地遥测：

```c
int dp_submit_sig_value(struct dp_key key, const uint16_t *words, size_t word_count,
                     k_timeout_t timeout, uint32_t *ticket);
int dp_submit_sig_values(const struct dp_point_update *values, size_t count,
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
等待使用条件变量广播，允许多个线程等待相同或不同票号，不再每毫秒轮询。
已完成票号使用 `K_NO_WAIT` 也返回成功；等待超时不会取消已提交的处理。
条件变量超时的锁处理与本工作区 Zephyr 4.4.2 实现保持一致，升级内核时须复核。

底层仍使用 `dp_publish()`；collect 通过此入口提交远端样本。硬件读取、
样本到信号键的映射和 IDU 十点快照组批留在应用层。参数写入使用 process/params
接口，事务内输出使用 `dp_txn_emit()`，不通过本地采样接口提交。

`CONFIG_DP_PROCESS` 默认开启，增加 WRITE/READ/INIT/SAMPLE/TIMER 事务、
异步消息队列和专用业务线程。handler 使用 `dp_txn_get()`、`dp_txn_emit()`
等事务访问器，不重入引擎。所有原始输入先准备完成，再执行 handler；
派生输出不递归调用 handler。关闭该配置时，带 handler/read_callback 的
配置会被拒绝，纯值转换和基础数据池仍可工作。

## 事务标志与提交

`process` 是持数据池锁执行的快速纯变换；事务 `handler` 使用 `dp_txn_*`
访问候选值、旧值和依赖状态，在数据池锁外执行。所有原始输入先准备完成，
派生输出不递归执行 handler。`dp_process_execute()` 同步返回结果，
`dp_process_submit()` 复制请求后在业务线程执行，完成结果指针只在回调期间有效。

| 标志 | 行为 |
| --- | --- |
| `DP_OP_NO_COLLECT` | 不下发，仍可更新缓存/持久值 |
| `DP_OP_NO_DM` | 仅下发，缓存和持久值保持原状 |
| `DP_OP_NO_STORE` | 更新缓存/下游，不更新持久目标值 |
| `DP_OP_DEFER` | 延后保存，1 s 防抖或显式 flush 时落盘 |

仅改缓存使用 `NO_COLLECT | NO_STORE`。NO_STORE 暂态值不会混入后续其他参数
快照。事务容量包含捕获的依赖及输出；所有输出先校验类型、范围和权限，
提交按设备/标志分组，后组失败不回滚先组，未执行项报告 `-ECANCELED`。
持久远端点的 NO_DM/NO_STORE 仍需意图日志对账；非持久下发不使用日志或自动重试。

`dp_process_save_reboot(delay_ms)` 在保存成功后延时重启；保存失败不重启。
参考回调内的 `dp_flush_para_store()` / `dp_set_reboot()` 先设置标记，
仅在事务成功后执行。多设备默认恢复和外设直接写入同样不提供整体回滚。

## 声明式初始化和定时器

模块提供初始化机制，由应用明确决定装配顺序。IDU 在 `app/main.c`
组织启动流程：先注册配置并调用 `dp_init()`，建立采集接口并注册全部总线，
再初始化/恢复参数，由应用判断存储故障是否允许降级，最后调用
`dp_process_init()`、`dp_reference_init()` 和 `co_polling_init()`，再由应用放行处理任务。
北向站号在本地初始化确定，显式初始化转发服务后由独立任务维护，
不使用 `SYS_INIT` 隐式决定这组依赖顺序。

原格式全局 INIT/TIMER 表通过 `pltcb_reg_cfg_get_*` 成对注册，由
`dp_reference_init()` 校验并启动。INIT 按表序执行，负返回值使本次启动失败；
全部 INIT 成功后才启用 type=0 周期定时器。定时工作向 DP 队列提交请求，
业务函数在 DP 线程执行。原格式表与下面的按点计划是两组显式入口，避免重复注册同一业务。

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
int err = dp_process_init(&plan);
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
`co_init_bus()` 不再注册参数后端；应用显式选用
`co_parameter_transport()` 或自定义后端并交给 `dp_params_set_transport()`，再初始化参数。
参数初始化后，采集总线登记不会替换应用已经选定的参数后端。

回调处理设备键对应的下发。非持久命令只需 write，持久设定值还需 read。读写可阻塞在所属传输线程上，但不能
重入参数引擎。缺少后端时远端写入在建立持久化意图前返回 `-ENODEV`，
缓存、持久值和本地参数操作不受影响。模拟来源由可选回调报告，默认 false。

持久远端写前保存意图，丢失 ACK 或重启后只通过回读对账，不重放写入。
非持久命令直接写一次，存储故障不阻止这类写入，失败保持缓存旧值。
FLAG_COLLECT 与 FLAG_PERSISTENCE 独立；非零周期可同时轮询，反馈只更新缓存。
显式 dp_params_sync_device() 只同步持久目标值，不使用反馈值，也不重发一次性命令。
NVS 使用版本 2 的单参数记录，以 `(dev_id, sig_id)` 独立索引，仅保存设定值及必要元数据。
默认值、上下限和回调归 const process 表；新增或重排参数无需匹配全表签名。
整批提交通过临时日志保证掉电后补完；无变化的 flush 不写 Flash。
`CONFIG_DP_PARAMS_TRANSACTION_BYTES` 控制临时批量载荷，替代旧 SNAPSHOT_BYTES。
存储格式不兼容时自动清空所配置的参数分区，记录警告并恢复当前默认值，设置
`DP_PARAM_STORAGE_RESET`；不保留、解析或迁移旧快照正文。Flash I/O 错误则报错停写。
`dp_param_status.error` 表示最近一次参数操作结果；`storage_error` 独立记录存储不可用原因。
成功的非持久操作不会清除存储故障，只有存储重新就绪（如成功 reload）才清除；
应用可直接用该字段报告降级状态。该字段只在 RAM/API 状态中，不改变 NVS 记录或协议寄存器布局。

同类型长度变化可由既有 mismatch 回调转换；拒绝则重新初始化。范围失败不自动擦除。
详见[记录布局与恢复边界](../../docs/point-tables.md#参数下发与持久化)。
关闭参数后端时参数点配置被拒绝，`dp_process_save_reboot()` 返回 `-ENOTSUP`。

`tests/dataprocess` 仅注册此模块，覆盖无 Flash、仅本地 NVS、注入假传输
三种配置，以及 INIT 失败释放槽、初始化顺序和单次启动约束。

## 参考服务与 const 配置

设备及三表具有静态生命周期，可以放在 Flash。原表查询返回 `const` 指针；
没有原协议表的 native 配置不会被强转成参考结构。运行值、质量、时间戳、
事务和存储缓冲保存在 RAM。

`dp_set_para_def_range()` 复制默认值/范围到有界覆盖槽，保留原 const 表；
大于 4 字节的默认值 getter 需要调用方提供缓冲区。覆盖不持久化，
不立即修改当前值。`dp_dm_reset_dev_data()` 仅重置远端遥测，旧排队样本不能覆盖重置结果。

`dp_para_reg_set_func()` 与 `dp_set_para_not_dm()` 是直接采集下发路径，和前述
NVS transport 分开；超时后保留已接受请求的副本直到完成，防止迟到回调引用失效内存。
策略、迁移、变化/日志通知、默认恢复和重启的准确上下文及容量限制见
[参考平台对照](../../docs/reference-platform.md#回调存储与直接下发的约定)。

`tests/reference_services` 分别验证有/无参数存储的服务配置；
测试方法与历史 RAM 数值统一见[验证说明](../../docs/testing.md)。

启用 `CONFIG_DP_REFERENCE_TABLES` 时，原格式三表按需转换为单条执行视图，
不会复制整张表到 RAM。`dp_config_point()`、`dp_config_collect()`、
`dp_config_transmit()` 和 `dp_find(key, &scratch)` 的结果可能指向调用者的
scratch，不能跨其生命周期保存。采集视图的 `fields` 也指向该 scratch 内部，
不能浅拷贝后保留。原生静态配置仍直接返回其 const 行。
Modbus 采集表直接配置 `uint16_t period_ms`，执行视图按原值读取，无周期转换注册。
0 表示不周期采集，初始化读必须显式设置 `FLAG_INITIALIZE`；未标记的零周期目标不加入自动采集。
默认/范围、恢复默认策略和 UPP 模型周期转换函数须保持只读和确定性，可能在运行期被反复调用。

`dp_config_index()` 先定位设备，再查找该设备的处理表；配置查询与数据池读写
共用这一路径，不扫描其他设备的信号行，也不为索引另建 RAM 表。
表行可以无序，返回值保留原始全局行号；点表顺序和参数存储布局不受影响。
原生配置继续按原数组查找。

## 分散点表登记

可选 `DP_TABLE_REGISTRY` 使用 Zephyr Iterable Sections 汇集整张表的 const 描述符。
应用各文件显式定义 static const 数组并登记一次，数量使用 ARRAY_SIZE；
`dp_table_registry_bind()` 按标识及预期类型装配设备关联，检查重复、缺失和长度溢出。
表内容不复制，调用方提供的 DEV_TABLE_S 小型指针视图在服务期间保持有效且不可修改。
`tests/table_registry` 覆盖跨文件发现、错误绑定、非持久命令、故障和持久反馈。
