# 点表与协议配置

[文档导航](../../docs/README.md) · [点表与寄存器](../../docs/point-tables.md)

本目录声明产品点表、设备关联、协议服务配置和端口编排。
采集、转发、参数存储及协议解析由各自模块执行。这里只维护 IDU 产品方案，
IAPv3 的启停只影响升级协议段，不切换业务点表。

```text
protocol/
├── protocol.c       # 数据设备与独立转发入口的静态关联
├── init.c           # 模块连接、参数加载/恢复和统一任务放行
├── tasks.c          # 应用创建 DP/CO/TR 执行上下文与通信维护
├── tasks.h          # 内部任务编排接口
├── ports.c          # 实际端口映射、共享 RS485 事务锁及收发编排
├── ports.h          # 内部链路标识
├── tables.h         # 内部表标识及登记类型引用
├── inc/             # 只保存对外头文件
│   ├── protocol.h
│   └── signal_ids.h
└── tables/
    ├── process.c    # 信号、标志、回调、默认值及范围
    ├── collect.c    # 南向地址、字序、轮询周期及写入目的地
    ├── transmit.c   # 北向寄存器到设备/信号的映射
    └── segments.c   # 可选 IAPv3 协议地址段与回调
```

`forwarding.c` 和 `parameters.c` 已删除。参数状态/命令由 `tables/process.c` 绑定回调，
`tables/transmit.c` 映射北向地址；`tables/segments.c` 通过独立转发入口的
协议段配置绑定 IAPv3，`protocol.c` 选择数据设备、转发表和广播策略。
参数 Flash 保存实现位于 DP 模块，升级暂存和验签位于 IAP 模块。
`main.c` 启动硬件、状态和可选 IAP 后调用 `idu_protocol_init()`。
协议层完成本地装配后才放行采集和处理任务，不等待远端上线，不通过 SYS_INIT 隐藏业务依赖。
本地采样只查询已锁存的协议维护故障，不承担首次转发启动或周期维护。

## 分散登记与设备关联

三张业务点表及可选协议段表由 CMake 独立编译。每张表是普通 `static const` 数组，使用参考结构显式写行，
文件内通过 `STRUCT_SECTION_ITERABLE(dp_registered_table, ...)` 登记一次。
登记项保存表标识、协议类型、数组指针及 `ARRAY_SIZE` 得到的条目数。
不使用逐行宏，不维护 extern 数组及手工数量枚举，不包含其他 `.c` 文件。

应用点表逐字段显式初始化，`data` 中的默认值、最小值和最大值也分别写出；
联合体选择与实际数据类型对应的成员。没有专用配置时使用所属模块提供的字段默认值：
DP 的 `DP_POINT_DEFAULT_*`、collect 的 `CO_TABLE_DEFAULT_*`、
transmit 的 `TR_TABLE_DEFAULT_BYTE_ORDER`。这些是编译期字段常量，不隐藏整行配置，
不增加初始化调用或 RAM 模板。地址、设备/信号 ID、类型、长度和轮询策略仍由应用填写。

数据设备使用 `data_device_cfg`，逐项声明 process、可选 collect、采集总线和南向站号。
`collect_table=DP_DATA_DEFAULT_COLLECT_TABLE`（零）表示不生成采集目标，并忽略采集口和站号。
`idu_register_point_tables()` 先调用 `dp_data_device_bind()`，再登记 pltcb 数据目录回调。
模块按表标识解析，不依赖链接顺序，缺表、错误类型、重复身份及非法采集关系明确失败。

北向使用独立 `tr_forward_cfg idu_forwarding`，声明转发身份、逻辑口、普通转发表、可选协议段和广播策略。
缺省段标识和广播策略使用 TR 的 `TR_FORWARD_DEFAULT_*`，无需 process 或七槽占位。
协议层先通过 `tr_forward_init()` 显式装配 RAW 服务，之后由应用维护任务调用 `tr_forward_service()`。
接收和重配置队列由应用创建并绑定，`ports.c` 登记实际收发回调。
北向站号由 `CONFIG_IDU_FORWARD_UNIT` 在初始化确定，不随后续 GPIO 样本变化。
没有绑定逻辑口时返回 `-ENOTCONN`，不会选择其他端口。

所有点表、默认值与配置声明在 Flash。DP 的小型设备关联视图、TR 的入口映射/生命周期视图
留在 RAM，不复制完整点表；执行行仍在调用者栈上按需转换。
新文件仍须加入 CMake；链接段不会自动扫描源码目录。
两个北向入口可以复用地址映射不同数据，也能共用同一张表；普通读写及协议段均按入口选择。
详细边界见[转发表与北向服务](../../docs/forwarding-model.md)。

当前 `protocol.c` 的 `IDU_REMOTE_DEVICE` 宏只复用同型设备挂表结构；
`LISTIFY(2, IDU_REMOTE_DEVICE, (,), 0, IDU_DEV_REMOTE_IO, 1)` 在预处理期展开为
设备 10、11，南向站号 1、2，共用采集端口 0。调用旁逐项解释了全部参数。
三张测点表仍逐字段填写，两个远端共享只读 process/collect 表，运行状态各自独立。
北向转发表仍引用原来的设备 10，不自动新增设备 11 的北向寄存器。
本地加两台远端共 40 点、10 个采集项、7 个持久参数；点值实际申请 162 B，
产品在 `prj.conf` 中设置 256 B 的 DP 系统堆预算，仍需另计堆管理开销。

## 修改设备或测点

1. 在信号头文件定义 ID，在 `process.c` 添加信号及业务回调。
2. 在同一处理表行填写 `data` 默认值及上下限，通过 `sig_flag` 控制范围检查和恢复策略。
3. 南向采集或写入目的地放入 `collect.c`；北向寄存器映射放入 `transmit.c`。
4. 新增整张表时，在文件末尾登记一次，并在内部 `tables.h` 分配唯一标识。
5. 在 `protocol.c` 的 `bindings[]` 选择设备要使用的表和南向地址。
   多个设备可共享相同表标识；北向映射显式指定各自设备/信号。

逻辑信号 ID 统一在 `inc/signal_ids.h` 定义。本地与南向设备共用同名信号，
由设备 ID 区分测点；协议寄存器地址仍由采集表、转发表分别填写。

当前产品周期采集三点：南向 DI（04/8152）、模拟量 1（04/8163）、模拟量 2（04/8164）。
采集表直接填写 `uint16_t period_ms`，单位为毫秒，最大 65535 ms（约 65.5 秒），无需周期编码或转换函数：

- `.period_ms = CONFIG_IDU_COLLECT_PERIOD_MS`：使用 `.conf` 配置的周期，目前为 500 ms。
- `.period_ms = 2000`：该行独立按 2000 ms 采集。
- `.period_ms = 0`：不周期轮询；初始化读必须同时设置 `.flag = FLAG_INITIALIZE`，仅下发点不加该标记。

`CONFIG_IDU_COLLECT_PERIOD_MS` 在 `prj.conf` 手动配置，不按固件类型覆盖。
协议号和南向地址显式标记初始化且周期为 0，每次上线时采集一次。北向普通读访问 DP 缓存。
本地模拟量仍使用现有电流换算（0.01 A）；改为电压单位需同步业务换算和协议约定。

## 设置回调、读取回调和地址段

沿用参考工程的 `DM_SIG_CFG_S.pf_sig_proc`：正常设置走 `DP_PARA_SET`，
初始化走 `DP_PARA_INIT`。仅在设置 `FLAG_READ_CALLBACK` 时，协议读和
`dp_fast_para_get()` 才以 `DP_PARA_GET` 调用回调；`dp_get_sig_value()` 仍直接读缓存。
该 readback 指按需读取回调，与南向持久参数写后的读回确认是两个机制。

- 31090–31093：四个非持久命令点，回调 `idu_parameter_command()` 校验触发值，
  用 `dp_defer_para_action()` 登记一次动作。只接受单点写入（FC06 或单字 FC16），
  DP 完成回调和范围校验后同步执行，执行完成才应答；不缓存命令触发值，不在 INIT/GET
  或重启时执行。一次请求不能包含其他命令、参数或派生写入，失败不会自动重试。
- 32000–32003：一个 8 字节 `DATA_TYPE_U16ARRAY` 点，设置 `FLAG_READ_CALLBACK`，
  回调 `idu_parameter_status()` 在 GET 时生成标志、错误、序号低字和高字。
  完整读取或读取其中一部分都从同一次状态快照截取，SET 返回拒绝。
- 普通值回调只校验、转换或输出候选值；恢复默认、保存等不可撤销动作通过 DP 延后动作
  接口登记，不直接在校验回调中执行 I/O。接口只接受单个本地非持久写点，不允许派生写入
  或同时登记其他完成动作；可读取其他点辅助校验。
- `tr_forward_cfg.protocol_segments` 直接选择 `MB_TABLE_SEG_T` 协议段；
  IAPv3 使用 20003、20049–20104、31996–31997 三段，填写实际线地址。
  段按请求首地址匹配，在标准 PDU 校验前调用 `iap_route()`，保留变长升级包。
  回调负责精确长度、功能码、会话及广播行为；命中错误不回退到点表。
  注册时拒绝空回调、非法区间、重叠段和重复的协议段分组。
- `tr_register_service.routes` 保留标准 `tr_segment` 地址段，执行完整寄存器范围检查；
  协议段未命中时进入此处及普通转发表。产品当前没有额外的标准透传段。
  `route_broadcast` 显式允许协议段收到广播，段外广播保持静默且无副作用。

详见[回调归属决策](../../docs/adr/0002-process-point-callbacks.md)及
[协议段决策](../../docs/adr/0003-iap-protocol-segments.md)。

## 下发、轮询与掉电保存

- `FLAG_COLLECT`：北向写入通过采集表找到南向目标，交给 collect；不表示开启轮询。
- `FLAG_PERSISTENCE`：将参数保存到 NVS。低频配置使用 `FLAG_PERSISTENCE_SYNC` 或
  `FLAG_RT_STORE`，成功回复前保存；未设置这两个标志时，保留原有延后保存语义。
- 带 `FLAG_COLLECT` 的 Modbus 行，period_ms 为 0 为仅下发，非零同时周期采集；
  初始化读由独立 `FLAG_INITIALIZE` 标记，周期 0 本身不表示初始化。`flag` 的写入位明确声明允许的写功能码。

不持久化的下发只发送一次，不要求前置读回，不写意图日志，失败不更新缓存，重启不重放。
持久化下发保留“记录意图—写设备—读回确认—保存”的原事务；不确定状态通过读回恢复。
可轮询写入点的反馈只更新实时缓存，不覆盖已保存的设定值；旧排队样本不能覆盖更新的写值。
一次写入不能混合持久和非持久南向点，以免把一次性命令纳入掉电重放语义。

当前共七个持久参数，按 `(dev_id, sig_id)` 分别保存设定值，不保存默认值和上下限。
新增缺失参数使用当前 process 默认值；改变排列、设备数量或下发地址不清空已有值。
参数键改变视为新参数，旧键记录暂留。原演示南向设置地址仍为 40000–40002。

**格式不符合当前版本时自动清空 `idu-settings` 参数区并重新初始化**，包括旧整体快照和
无法转换的类型/长度。记录警告日志，状态 bit4 表示本次重新初始化；旧参数值及未决意图均不迁移，
不重放南向命令。I/O 故障则报错停写。详见[参数持久化](../../docs/point-tables.md)。
质量寄存器和样本年龄过期暂缓，见[根 README](../../README.md)。

## 行内参数与国家差异

`DM_SIG_CFG_S` 的 `data` 直接包含 `DM_SIG_DATA_S`，范围和恢复开关统一使用 `sig_flag`。
默认值、范围、类型和业务回调在同一行维护，例如：

```c
{.sig_id = IDU_SIG_SAMPLE_PERIOD, .sig_size = 2,
 .sig_flag = FLAG_PERSISTENCE | FLAG_RT_STORE | FLAG_RANGE_CHECK,
 .data_type = DATA_TYPE_U16, .pf_sig_proc = DP_POINT_DEFAULT_CALLBACK,
 .data = {.def_value.u16 = 500, .min_value.u16 = 100, .max_value.u16 = 1000}},
```

- 持久参数默认允许主动恢复；设置 `FLAG_NO_RESTORE` 后恢复时跳过该点，
  仍可正常保存、写入和启动加载。无需额外的恢复策略字段。
- `FLAG_RANGE_CHECK` 同时启用最小/最大值检查，仅用于整数标量。开启且双零时只允许零；
  未开启时使用类型范围，即使填写非零上下限也不启用行内限制。
  `dp_set_para_def_range()` 的显式运行期覆盖独立生效。
- 省略 `data` 时初值为零。
  大于四字节的初值使用静态 const 数组，NULL 指针由 DP 提供全零默认。
- 设备号由设备挂表决定，共用处理表的设备直接共享配置，不再寻找型号首个实例。
- 点表常驻 Flash；`dp_set_para_def_range()` 的 RAM 动态覆盖仍独立，不能改写配置表。

默认/范围解释和恢复策略查询归 DP，不再注册应用查表回调。只读查询使用
[`point_parameters.h`](../../modules/dataprocess/include/dataprocess/point_parameters.h)：

```c
DM_SIG_DATA_S data;
bool restore;
int err = dp_point_parameters_get(DP_COUNTRY_COMMON,
                                  IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD,
                                  &data, &restore);
```

当前产品使用 `DP_COUNTRY_COMMON = 0`，没有具体国家数据或自动切换行为。
以后可在 `dp_init()` 之前调用 `dp_register_parameter_country(country, override)`。
覆盖回调收到行内通用值；返回 0 使用覆盖值，`-ENOENT` 表示已知国家无该点差异、
整体回退通用配置，`-ENOTSUP` 表示未知国家。回调只读、确定、非阻塞，不重入 DP；
数组指针生命周期覆盖配置使用期。需要国家差异范围的点必须启用 `FLAG_RANGE_CHECK`。
DP 初始化成功后固定选择，拒绝重新注册。
旧默认/范围回调与国家覆盖互斥，避免两份默认来源。

查询不修改当前值、不读写 NVS，也不应用 RAM 动态覆盖。未知设备/信号返回
`-ENOENT`，未知国家返回 `-ENOTSUP`；失败不改输出。国家选择会影响初始化的默认值
和范围，但已有 NVS 继续按现有规则校验并优先加载，不自动恢复默认或迁移存储。
详见[处理表参数决策](../../docs/adr/0004-inline-process-parameters.md)。

## 同线 RS485 联调

产品不编译模拟从机。`ports.c` 将 CO/TR 两个 RAW 逻辑口接入同一个 USART2/RS485。
当前南向地址为 1、2，北向在 .conf 显式配置为 16，必须避免同线地址冲突。
拨码采样仍是本地测点，不自动修改本次初始化确定的北向站号；设备 ID、从机地址和寄存器地址相互独立。
本机锁覆盖南向请求到响应/超时，不具备外部多主仲裁；等待南向响应时不匹配的北向帧会被丢弃。
外部主站需留出采集窗口，实际性能和 DE 时序仍需实板测试。
详见[端口说明](../../docs/communication-ports.md)。

## 任务与设备状态

`init.c` 完成表注册和 DP 初始化，显式连接 CO 样本/掉线及 DP 参数后端，准备挂起任务，
建立物理接入与总线，逐设备登记策略，处理参数加载/未决恢复，再装配业务计划及北向服务，最后放行 DP/CO。
参数区不可用时保留可用通信和本地采样，应用状态报告存储错误；持久写与 IAP 提交保存继续失败。

`tasks.c` 只为实际一条采集总线和一个北向入口创建上下文：DP 样本/业务、CO 线程、TR 接收/配置队列。
模块保留调度和处理入口，不自建这些线程。TR 维护在配置队列独立运行，不等待本地采样周期。

远端策略为额外重试 2 次、最大异常 3000 ms、掉线首包探测 5000 ms，来自 `.conf` 手动配置；
初始化组与正常组显式分开，按声明顺序合并连续且兼容的采集项，不跨地址空洞。
掉线仅探测首包，成功后继续其余初始化包；有效异常有通信但报错，显式写失败不自动重试。
自动上线不重放已保存目标或业务初始化回调。

掉线通过注册的 `dp_invalidate_acquired_device` 保值失效；排队旧样本不能恢复质量，重新采到的点分别恢复。
协议层将 CO 设备阶段和参数存储错误接入 `app/status`，远端离线本身不停止整机喂狗。
完整边界及验证见[通信重构](../../docs/communication-refactor.md)。
