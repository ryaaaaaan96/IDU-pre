# 同一采集端口挂载多个设备：配置方案调研

调研日期：2026-10-21。应用基线：`59b2dfd`，Zephyr 工作区版本 `v4.4.2`。
本页是建议，尚未修改配置结构、装配接口或实际设备数量，也未构建 32 台设备固件。

后续实现已选择静态设备宏与 `LISTIFY()`，当前为两台远端；没有引入本页建议的模型/区间接口。
DP 点值改为 Zephyr 系统堆按实际字数一次申请。当前配置见
[产品配置说明](../app/protocol/README.md)，下文的 32 台核算仍为基线方案分析。

## 结论

32 台设备共用一套处理表和采集表时，建议将配置分成**设备模型模板、端口分组、设备实例**：
表关联和设备型号只写一次，实例只写逻辑设备 ID 与通信站号。地址连续时可以另提供区间展开；
地址不连续或存在异型设备时保留短实例列表。最终仍装配现有固定七槽 `DEV_TABLE_S`，
不需要同时修改 `dp_/co_/tr_` 点表接口，也不需要重新讨论七槽数组的压缩。
这是根据当前绑定器和执行视图作出的设计建议，不是现成 Zephyr API。
[当前绑定结构与装配契约](../modules/dataprocess/include/dataprocess/table_registry.h)、
[固定设备视图](../modules/dataprocess/include/dataprocess/reference_tables.h)。

## 当前代码已经具备的复用能力

- `dp_table_bind()` 只校验同一设备内的重复挂表，不禁止不同设备使用同一个表 ID。
  装配时复制表指针与数量，不复制表内容，因此 32 个设备可以指向同一份 Flash 处理表和采集表。
  [来源：table_registry.c，第 30–60 行](../modules/dataprocess/src/table_registry.c#L30)。
- 处理点身份来自 `{设备 dev_id, 行 sig_id}`，采集请求的设备身份来自所属 `DEV_TABLE_S`，
  站号来自 `coll_addr`。同样的信号 ID 和寄存器地址可以在不同设备重复使用。
  [来源：reference_point()](../modules/dataprocess/src/reference_config.c#L387)、
  [reference_collect()](../modules/dataprocess/src/reference_config.c#L443)、
  [compile_tables()](../modules/dataprocess/src/reference_config.c#L594)。
- 点值存储按实例展开后的点索引分配，在线状态按采集设备索引分配，轮询截止时间和开关按采集项索引分配。
  共享 const 配置不会让这些运行状态合并；32 台设备仍需 32 份状态。
  [来源：points.c 的 states/point_words](../modules/dataprocess/src/points.c#L14)、
  [collect.c 的 statuses/next_poll/disabled](../modules/collect/src/collect.c#L31)。
- 共享的是同一套默认值，不会按实例自动改写。当前 `SIGNAL_ADDRESS` 默认 1，
  实例 `coll_addr=4` 不会让该测点默认值变成 4；初始化读取成功后才有真实采样。
  未采到时不能把默认 1 视为该实例的真实地址。实例特有默认值需另设明确机制，不能修改共享 const 行。
  [来源：远端 process 表](../app/protocol/tables/process.c#L324)、
  [初始化采集行](../app/protocol/tables/collect.c)、
  [point_values() 默认值装配](../modules/dataprocess/src/reference_config.c#L275)。
- 当前转发表 `MB_TABLE_CELL_T` 自带来源 `dev_id`，`reference_transmit()` 原样使用它。
  不能把一份指向设备 10 的转发表挂载 32 次，就认为会自动引用每个实例。
  北向地址如何映射到 32 个设备，仍需独立编排；模型复用首先用于 process/collect。
  [来源：MB_TABLE_CELL_T](../modules/dataprocess/include/dataprocess/reference_tables.h)、
  [reference_transmit()](../modules/dataprocess/src/reference_config.c#L516)。

## 建议的配置归属

| 层次 | 配置内容 | 不应隐含的行为 |
| --- | --- | --- |
| 模型模板 | `dev_code`、完整七槽表关联，首槽 process，其余 collect 等 | 不含某台设备的逻辑 ID、站号 |
| 端口分组 | 采集逻辑 `port`，默认使用的模型模板 | 不重新打开 32 个物理串口或创建 32 个总线线程 |
| 实例 | 全局唯一 `dev_id`、当前端口内唯一 `coll_addr` | 不把逻辑 ID 自动等同于 Modbus 站号 |
| 异型分组 | 同一端口上的其他模型模板及其实例列表 | 不用大量逐字段覆盖制造隐式继承 |
| 端口接入 | `ports.c` 的实际 UART/vport 收发编排 | 不放进设备模型或通用 DP 装配器 |

这些归属保留现有 `dev_id`、`port`、`coll_addr` 的含义。具体字段来自
[dp_device_binding](../modules/dataprocess/include/dataprocess/table_registry.h#L28)，
物理接入职责来自 [ports.c](../app/protocol/ports.c#L59)。

### 连续的 32 台设备

以下是**建议接口，尚未实现**。`remote_io_model` 包含现在远端配置的 `dev_code` 和完整 `.tables`，
这部分只写一次；本地设备可以保留现有单独配置，不必强行纳入批量配置。

```c
static const struct dp_device_range remote_io_range = {
    .model = &remote_io_model,
    .port = 0,
    .first_dev_id = 10,
    .dev_id_step = 1,
    .first_address = 1,
    .address_step = 1,
    .count = 32,
};
```

展开后的逻辑 ID 为 10–41，南向站号为 1–32。逻辑 ID 不因现场地址重排而自动改变。
当前本地逻辑 ID 为 1，不与该范围冲突；但当前北向地址由本地采样决定，必须避开南向站号 1–32。
示例仅展示映射，未代替现场地址配置。
[当前 ID 定义](../app/protocol/inc/signal_ids.h)、
[同线地址冲突检查 collect_transfer()](../app/protocol/ports.c#L59)。

### 地址不连续或设备数量变化

以下也是**建议接口，尚未实现**。每个设备仍然占一行，但只写真正不同的两个字段，
不再重复七个槽位及型号配置。需要 32 台时列出 32 行身份映射即可。

```c
static const struct dp_device_instance remote_io_instances[] = {
    {.dev_id = 10, .coll_addr = 1},
    {.dev_id = 11, .coll_addr = 4},
    {.dev_id = 12, .coll_addr = 9},
};

static const struct dp_device_group remote_io_group = {
    .model = &remote_io_model,
    .port = 0,
    .instances = remote_io_instances,
    .count = ARRAY_SIZE(remote_io_instances),
};
```

另一种设备使用另一模型和分组，允许同样的 `port=0`，但通信站号不能重复。
优先使用这种稀疏实例列表作为基础能力；区间只用于确实连续、稳定的安装场景。
这两种方式无需引入整行宏，与当前逐字段显式配置约定相容。

## 装配器必须保留的约束

建议由 DP 模块提供通用模型/实例装配，应用只声明配置。不要在应用手写 32 次复制逻辑，
也不要为了复用模板永久保留一份 RAM `dp_device_binding[33]` 再复制成另一份设备视图。
可共享校验和填充逻辑，直接生成现有 `DEV_TABLE_S[33]`；具体实现需单独设计和测试。

装配前必须校验全部输入：首槽必须为 `PRO_DATA_PROCESS`；表 ID 存在、类型匹配；
同一设备不能重复挂同表；空槽保持 ID/type 同为零；所有分组的 `dev_id` 全局唯一；
同端口的站号唯一且为 1–247；区间计算先用较宽整数检查溢出再转换为 8 位；
输出容量足够。全部验证通过后才能写 output，保留当前“失败不修改 output”的契约。
现有 `dp_table_bind()` 保证表关联校验和输出原子性，站号唯一性在后续 `dp_validate()` 中检查；
新批量入口应明确校验边界，不能只展开后遗留部分结果。
端口是否已经登记继续由 CO 启动检查，DP 装配器不依赖具体端口后端或 CO 服务。
[来源：装配器](../modules/dataprocess/src/table_registry.c#L15)、
[dp_validate() 设备检查](../modules/dataprocess/src/points.c#L133)、
[对外生命周期契约](../modules/dataprocess/include/dataprocess/table_registry.h#L36)。

## 32 台设备需要同时核对的容量

以当前表内容原样复用、本地设备保留为前提，源码核算如下。不是已经通过构建或实板验证的配置。

| 容量项 | 当前默认值 | 32 台远端＋1 台本地的需求 |
| --- | ---: | ---: |
| `DP_MAX_DEVICES` | 16 | 32 个采集设备；设 32 可容纳 33 个关联视图 |
| `DP_POINTS_MAX_POINTS` | 64 | `26 + 32 × 7 = 250` |
| `DP_POINTS_VALUE_WORDS` | 512 | `63 + 32 × 9 = 351`；当前 512 足够 |
| `DP_POINTS_MAX_POLLS` | 16 | `32 × 5 = 160`，含 96 个周期项和 64 个初始化项 |
| `DP_PARAMS_MAX_COUNT` | 16 | `3 + 32 × 2 = 67` |
| `DP_PARAMS_TRANSACTION_BYTES` | 2048 | 当前内容编码为 `16 + 38 + 32 × 18 = 630 B`，当前上限足够 |
| `CO_MAX_BUSES` | 1 | 仍为 1，不因同端口设备数变为 32 |

核算来源：[process 表（本地 26 行、远端 7 行）](../app/protocol/tables/process.c)、
[collect 表（3 个周期、2 个初始化、2 个仅下发项）](../app/protocol/tables/collect.c)、
[默认容量](../modules/dataprocess/Kconfig#L75)、
[只统计挂载采集表的设备、关联视图允许 MAX_DEVICES+2](../modules/dataprocess/src/reference_config.c#L534)、
[参数计数及临时事务载荷公式](../modules/dataprocess/src/params.c#L108)。
`DP_MAX_DEVICES` 不是所有关联视图数量的直观同义词，这是现有实现的边界，而非新增建议。
需在 `.conf` 手动调整，不能只简化配置语法后直接启用 32 台设备。

共享表只节省重复定义，不能消除运行状态。当前 ARM 设备视图按 64 B/设备估算，
33 份为 2112 B；扩大的点状态、设备状态、轮询截止时间和参数缓存还需额外 RAM。
尤其参数模块按 `DP_PARAMS_MAX_COUNT` 同时预留 `cache`、四份 `dp_value` 数组、索引和 dirty 标志，
67 个参数的成本远高于单纯把临时事务载荷从少量参数扩到 630 B。实际净 Flash/RAM 增量须用目标构建核对，
不能根据配置行数承诺节省。
[来源：设备视图定义](../modules/dataprocess/include/dataprocess/reference_tables.h)、
[点状态池](../modules/dataprocess/src/points.c#L14)、
[参数缓存数组](../modules/dataprocess/src/params.c#L26)。

## 轮询、转发和持久化的实际影响

1. **一条总线仍串行处理事务。** CO 按总线创建线程和栈，不按设备创建。
   当前每条 collect 行生成独立请求，不会自动将 8163/8164 合并；
   32 台设备每轮需要 96 次周期读取。500 ms 是每项事务完成后的调度间隔，不能保证
   32 台设备在同一 500 ms 内全部刷新。超时默认 100 ms，仅 96 次无响应等待就是约 9.6 秒，
   尚未计发送与调度开销。这是坏链路预算示例，不是实测结果。
   [来源：reference_collect()](../modules/dataprocess/src/reference_config.c#L443)、
   [collect_thread()](../modules/collect/src/collect.c#L470)、
   [超时配置](../modules/collect/protocols/modbus/Kconfig)。
2. **扫描成本也会增长。** 每次调度遍历全部采集项，而参考表 reader 每次按索引从头寻找所属设备/行。
   多设备下会放大配置查询 CPU 成本。可以后续评估小型设备起始索引、采集项定位索引和按总线索引，
   无需复制整张 Flash 表；只有测量表明需要时才实现。
   [来源：collect_thread() 的 dp_config_collect 调用](../modules/collect/src/collect.c#L484)、
   [reference_collect() 的顺序定位](../modules/dataprocess/src/reference_config.c#L443)。
3. **同线北向访问仍需协调。** 当前端口事务期间只把匹配回复交给 CO，其余报文返回未匹配；
   没有外部主站仲裁能力。增加南向事务占比会压缩北向可用窗口，分组配置不解决这个问题。
   北向来源设备 ID/寄存器映射也须另配置，不能自动从模型产生。
   [来源：ports.c receive()](../app/protocol/ports.c#L112)、
   [当前转发模型说明](forwarding-model.md)。
4. **保持参数逻辑身份稳定。** 当前参数存储以 `(dev_id, sig_id)` 独立寻址；新增实例或重排
   不使已有值失效，新增键使用默认值，删除的键暂留。修改类型/长度而不能转换或记录格式
   不兼容时，按当前策略自动清空参数区并重新初始化。不要复用旧键表示不同含义。
   [当前持久化规则](point-tables.md)、[架构决策](adr/0005-parameter-records.md)。

## 可选方案比较

| 方案 | 适用情况 | 本项目的判断 |
| --- | --- | --- |
| 逐台完整绑定 | 少量、各不相同的设备 | 当前两台清楚，32 台同型重复过多 |
| X 宏/整行初始化宏 | 接受预处理生成配置的工程 | 不推荐；掩盖字段，违背当前避免整行宏的约定 |
| 模型模板＋短实例列表 | 同型复用，地址可稀疏，现场可能调整 | 建议基础方案，实例身份明确 |
| 模型模板＋连续区间 | 同型、ID/地址可按规则分配 | 建议可选简写；例外增多时改用实例列表 |
| DTS 设备节点 | 固定板级外设或确需设备模型驱动的设备 | 能做，但当前现场协议设备的身份/表路由更适合应用配置，无需为减少 C 重复引入 binding |
| Iterable Sections | 多文件独立登记模型或设备分组 | 可组合使用；解决登记发现，不自动生成 32 个实例，也不提供运行状态隔离 |

DTS 判断依据是 Zephyr 对硬件配置与软件配置的分工，而非“DTS 不能描述外部设备”。
Iterable Sections 的能力是链接期收集相同结构的静态实例；当前点表已用它登记一次模型表。
分组若只有一个配置文件，普通 const 数组即可；需跨文件分散注册时再扩展 iterable 类型，
展开顺序必须显式约定，不能让链接符号排序意外改变持久参数顺序。
[官方 DTS/Kconfig 文档](https://docs.zephyrproject.org/latest/build/dts/dt-vs-kconfig.html)、
[本地 v4.4.2 对应说明](../../../zephyr/doc/build/dts/dt-vs-kconfig.rst)、
[官方 Iterable Sections 文档](https://docs.zephyrproject.org/latest/kernel/iterable_sections/index.html)、
[本地 API 对 const/ROM 的要求](../../../zephyr/include/zephyr/sys/iterable_sections.h#L24)。
外链是调研当日的 latest 文档，仅用于通用机制说明；具体版本行为以本地源码为准。

## 建议的实施边界

第一步仅增加模型模板和短实例列表装配能力，保留固定 `DEV_TABLE_S` 与现有点表；
第二步按实际需要加连续区间输入。启用 32 台前，单独完成容量配置、持久参数升级策略和
总线刷新时间预算。验证重点应包括共享表但状态独立、跨组重复 ID/站号、区间溢出、容量不足、
失败不改输出，以及已有持久数据重启恢复。当前调研没有实现这些变化，也没有实板性能证据。
