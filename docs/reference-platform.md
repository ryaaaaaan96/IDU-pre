# 参考平台接口与静态逆向记录

[文档导航](README.md) · [模块接口](../modules/README.md) · [验证与 RAM 记录](testing.md)

本次范围是参考工程 `/home/ryan/Documents/demo/MCUdemo/CODE` 的 DP、CO、TR 及其配置注册契约。公开类型和业务接口尽量沿用参考设计，线程、互斥、消息、定时和 Modbus 直接使用 Zephyr。没有引入 ARC 包装层，也不链接 `plateform.lib`。

以下结论来自公开头文件、应用调用和静态库的选定反汇编，不能据此声称还原了完整平台源码或实现了全部非 ARC 子系统。原库为 ARM 二进制，指针、位域和 `DATA_U` 的布局不构成新版的二进制兼容承诺。

## 复现证据

在 West 工作区根目录运行：

```bash
python3 applications/IDU-pre/tools/analyze_reference.py \
  --source /home/ryan/Documents/demo/MCUdemo/CODE \
  --output build/IDU-pre/reference-platform-audit
```

[审计工具](../tools/analyze_reference.py) 自动查找 GNU Arm `nm`、`objdump`、`ar`；也可传入 `--toolchain-prefix /path/bin/arm-none-eabi-`。源目录只读，输出必须位于参考工程和本应用目录之外。输出包括 `audit.json`、`audit.txt`、成员/符号清单及 `evidence/` 中的选定函数反汇编；临时对象文件用后删除。库、完整第三方源码和生成证据均不提交到产品仓库。

本次输入库 `platform/libs/plateform.lib`：

| 项目 | 静态检查结果 |
| --- | --- |
| SHA-256 | `ad98c824a43c06f505224ccf5a58bbc20e554f76ce4979bdd68819a931b00ed9` |
| 大小 / 成员 | 1,668,032 字节 / 114 个对象 |
| 四组前缀的导出函数符号 | DP 206、CO 190、TR 30、PLTCB 24，共 450；包含内部函数 |
| 四个公开头文件 | 91 处函数声明、89 个不同名称，均能找到导出符号 |
| 已选对象的调试信息 | 仅 `.debug_frame`，未发现 `.debug_info` / `.debug_line` |

公开接口清单仅扫描 `data_process.h`、`collect_lib.h`、`transmit_lib.h`、`platform_callback.h` 中的 `dp_` / `co_` / `tr_` / `pltcb_` 声明；它不是完整 C 预处理器。ARC 导出接口不列入此清单，但原实现中的 ARC 调用仍保留在证据中。函数位置记为“对象:函数+偏移”，偏移相对于对象内函数。未链接 ARM 指令显示的 `BL` 名称可能不准确，应以紧随其后的 `R_ARM_THM_CALL` / `R_ARM_THM_JUMP24` 重定位目标为准。

## 项目、配置与启动关系

| 参考工程路径 / 职责 | IDU 对应位置 |
| --- | --- |
| `app/main.c:101` 注册设备、默认/范围、定时表，再于 `:142` 起依次启动 DP、本地设备、CO、TR | [app/process/points.c](../app/process/points.c) 显式装配；[platform_callback.h](../modules/dataprocess/include/platform_callback.h) 保存配置注册契约 |
| `app/cfg/equip/dev_cfg.c:43` 的 `DEV_TABLE_S` 实例组合各协议表 | [app/cfg/device.c](../app/cfg/device.c) 和 [inc](../app/cfg/inc) 的设备和型号配置 |
| `app/cfg/equip/table/{process,collect,transmit}.c` 分别描述数据、采集和寄存器映射 | [app/cfg/tables](../app/cfg/tables)，三表内容与处理执行分开 |
| `app/process` 的业务回调、`app/globalfunction.c` 的回调读写辅助 | [app/process](../app/process) 的业务处理，通用事务机制位于 DP 模块 |
| `app/port_filter`、`app/plc` 的报文处理和端口控制 | [app/communication](../app/communication) 的产品协议配置和 Zephyr 端口适配 |
| `platform/libs/plateform.lib` 的共享数据池、调度、采集、转发 | [modules/dataprocess](../modules/dataprocess)、[collect](../modules/collect)、[transmit](../modules/transmit) 的源码实现 |

参考工程的 `local_equip` 还包含产品功能。IDU 的采样映射、启动顺序和工程量换算继续属于应用。当前顺序为配置注册、`dp_init()`、采集接口、各总线 `co_init()`、参数恢复、`dp_process_start()`、`dp_reference_start()`、`co_start()`；TR 在地址就绪后绑定，产品依赖由应用显式安排。

## 配置契约与支持范围

[reference_tables.h](../modules/dataprocess/include/dataprocess/reference_tables.h) 保留原表结构的字段语义，使用标准整数和布尔类型：

| 原类型与来源 | 需要保持的契约 |
| --- | --- |
| `DEV_TABLE_S`，`data_process.h:316` | 设备 ID、端口、采集地址、型号及最多 7 个协议表；第一个表必须是处理表 |
| `DEV_TBL_CFG_S`，`:307` | `proto_type` + 条目数 + 对应类型的表指针，不是任意 native 配置指针 |
| `DM_SIG_CFG_S`，`:261` | 信号 ID、**字节数**、标志、数据类型、处理函数；自身没有默认值和范围 |
| `DM_SIG_DATA_S`，`:272` | 默认值、最小值、最大值；超过 4 字节的默认值通过 `DATA_U.pst` 提供 |
| `MODBUS_RTU_TABLE_S`，`collect_lib.h:64` | 信号、地址、寄存器数、功能码、查询频率编码、标志、优先级和字序 |
| `MB_TABLE_CELL_T` / `_S`，`transmit_lib.h:66` | 外部寄存器到设备/信号及字序的映射 |
| `INIT_DATA_SET_S` / `TIME_DATA_SET_S`，`data_process.h:355–368` | 全局初始化/定时处理函数；定时周期为毫秒，调用时四个参数均为空 |

`platform_callback.h:37–54` 的原注册关系为设备表 getter、数量 getter，以及 `PARA_GET_RANGE_DEF(dev_id, sig_id, DM_SIG_DATA_S *, bool *recover_default)`。新版通过 `pltcb_reg_cfg_get_dev_tbl()`、`pltcb_reg_cfg_get_dev_num()`、`pltcb_reg_para_range_def_cb()` 注册，在 `dp_init(void)` 中校验并建立按需读取视图，成功初始化后冻结注册。实现见 [reference_config.c](../modules/dataprocess/src/reference_config.c)。

启用 `CONFIG_DP_REFERENCE_TABLES` 后，配置适配接收 U8、U16、S16、U32、S32、STRING、U8ARRAY、U16ARRAY，以及 `PRO_DATA_PROCESS`、Modbus MASTER/SLAVE 表以及下述 UPP 模型表子集。字节长度须精确匹配类型及配置容量。持久化本地点需要 `CONFIG_DP_PARAMS`，NVS 分区及恢复仍由应用启动。`FLAG_PERSISTENCE` 决定是否存储，FC03 映射本身不隐含 NVS；无持久标志的本地点仍可通过 FC03/06/16 运行回调并更新 RAM。默认/范围来自注册回调；未注册时初值为零并使用类型范围。回调给出的最小值和最大值均为零时，表示不额外检查范围，仍保留类型的完整取值范围，不将值限制为零。持久化点要求强制恢复默认值时会拒绝配置，避免静默覆盖已有 NVS 数据。

原 `rqst_cyl` 的头文件只写“查询频率”。反汇编 `co_port_mgr.o:co_porc_scan_list+0x70 / +0x112 / +0x11a` 可见计数递减和重装，不能直接当作毫秒。当前要求显式注册 `dp_register_collect_period()`，由产品将该编码转换为毫秒，零表示仅初始化读取。每个 Modbus 行独立请求，支持 `FLAG_FRAME`；不自动合帧。特殊采集数据类型、`scan_pri` 和未支持协议使初始化失败。`FLAG_COLLECT` 仅支持带明确 FC3 写目标的持久参数，由 FC06/16 下发并回读确认。

Modbus 字序采用 `arc_byte_order.h:60–64` 的实际宏：0 默认按 BIGSMALL、1 SMALL、2 BIGSMALL、3 SMALLBIG、4 BIG。0/2 对应 native 低字在前，4 对应高字在前，1/3 另需字节交换。UPP 的 `PUB_BYTE_ORDER_BIG=0` 是另一套编码，不能混用；无需引入该头里的 ARC 函数。

IDU 的三张应用点表也使用这些原结构，通过设备表及默认/范围 getter 注册。
`defaults.c` 提供原接口的 `DM_SIG_DATA_S`，回调直接写在 `DM_SIG_CFG_S.pf_sig_proc` 中，
信号 ID 使用宏或枚举；不再通过 `point_metadata` 字符串分发。
`dp_reference_prepare()` 可在启动线程前取得已校验配置。原生扩展
`dp_register_config_provider()` / `dp_init_config()` 仍供独立模块使用，与原表注册互斥。

`PRO_UPP_MASTER` 指向 `UPP_M_MOD_REG`，`sig_num` 是模型字段总数；支持
`cfg_mode=0`、读/读写字段、相同长度及类型、比例 1、无特殊优先级的配置。
周期必须经 `dp_register_model_period()` 显式解释。后端交换已解码的主机字序 word，
当前 IDU 只提供虚拟实现，不包含 UPP 帧编码、链路或设备管理。

原 INIT/TIMER 表使用 `pltcb_reg_cfg_get_init_reg_tbl/num()`、`pltcb_reg_cfg_get_timer_reg_tbl/num()` 成对注册。`dp_reference_start()` 在参数恢复及 DP 处理启动之后，先校验整组配置，再按表序执行 INIT，最后启用 TIMER。支持参考应用 `app/timer/timefunction.c:35–44` 实际使用的 type=0 周期定时，其他类型拒绝。Zephyr 延迟工作只向 DP 业务队列提交请求，业务回调在线程中执行，不占用系统工作队列。INIT 中途失败时，前面已成功的回调不会回滚；成功启动不能重复执行。

## 已核实的行为与新版边界

| 接口 / 证据 | 原行为与本次对齐 |
| --- | --- |
| `dp_set_sig_value(dev, sig, uint8_t size, uint8_t *value)`；`data_process.h:522`；`data_process.o:dp_set_sig_value+0x10 / +0x24 / +0x36` | 原库同步加锁、执行 `dp_set_para_proc`、解锁；参数是字节，不是 word 或队列 ticket。新版同步入口可写本地、远端遥测和允许写入的参数，回调完成后返回；不把它映射为仅本地的异步入队。 |
| `dp_get_sig_value(dev, sig, capacity, value)`；`:509`；`data_process.o:dp_get_sig_value+0x18 / +0x22`，`dp_mgr.o:dp_dm_get_value+0xe6 / +0x11e` | 原库只复制配置长度，却在成功后返回调用者传入的容量。新版保留该行为，尾部缓冲区不被填充；这是直接数据池读取，不运行 READ 回调。 |
| `dp_fast_para_get(PARA_DATA_S *)`；`:579`；`dp_para_set.o:dp_fast_get_para_proc+0x6a / +0x76 / +0xd8` | 必须只有一个条目且字节长度精确相等；有 READ 标志时调用 GET 回调。回调返回 0 时返回字节数；非零时保留原状态值及已生成的响应字节，允许调用者缓冲区更新，但不提交回调辅助接口产生的派生输出。native READ 入口把正状态转换为 `-EIO`。`DATA_U` 内联保存不超过 4 字节的值，更长值使用 `pst`。 |
| `DM_PF_SIG_PROC`、`FUNC_PARA_ALL`、`FUNC_PARA_S`；`:125–146` | 原回调接收输入/输出描述及新旧值；新版提供对应上下文和回调内辅助接口。`sig_size` 始终为字节，输入数组、旧值和输出复制由事务管理。 |
| `dp_sig_func_done`；`dp_para_set.o:+0xb2` | WRITE 回调返回非负值时继续，负值时中止。原实现执行 SET 回调后先写 DM 再转交采集，存在不回滚的路径。新版输出统一验证后提交，错误会被报告；不复现失败后仍部分改写数据池、忽略采集失败等缺陷。 |
| 全局 INIT/TIMER 回调；`dp_para_set.o:dp_timer_func_done+0x6a / +0x6c / +0x92` | 原库该定时路径调用回调后直接进入 `dp_para_set_dm`，未检查回调返回值。新版全局 INIT/TIMER 采用更严格的事务策略：非负值继续，负值中止本次事务且不提交派生输出；这项差异不是原库已有保证。 |
| `dp_set_para_value_flag()`；`data_process.h:648–660` | 原头说明重复输出替换，并在容量到达阈值时部分刷新。新版使用有界事务，容量不足报错，不以隐式部分刷新冒充事务成功。输出的 NO_SET_CO / NO_SET_DM / NO_STORE_DM 是独立位。 |
| `co_set_port_mode(port, mode, timeout)`；`collect_lib.h:137` | 原 timeout 单位毫秒。新版端口对应 `co_bus_cfg.id`，等待参数限制锁获取，正在执行的初始化仍返回忙；IDLE/INIT/UPDATE/IV/FAULT/TRAN 保留值 0–5。原 OTHER=6 不等于 native SLEEP=6，明确拒绝 OTHER。 |
| `co_set_scan_pkg_flag()`；`:248` | 原 ON=0、OFF=1；新版按设备、地址/功能码匹配切换周期采集，不反转语义，也不取消显式读取和初始化读取。 |
| `tr_reg_table_func(mode, callback)`；`transmit_lib.h:178`；`mb_slave_comm.o:tr_reg_table_func+0xe / +0x16` | 原处理器按模式全局注册，端口模式独立。新版同样提供全局模式回退，native 每端口处理器可覆盖。 |
| `tr_reg_pkt_filter_func(port, callback)`；`:196`；`mb_slave_work.o:tr_reg_pkt_filter_func+0x12 / +0x1a` | 原过滤器按端口注册。`app/port_filter/filter.c:218–227` 规定过滤返回 0 继续、4 已消费；`app/plc/tr_plc_func.c:92–103` 则规定模式回调 -1 继续、0 已处理，二者不可混用。 |

同步字节接口见 [data_process.h](../modules/dataprocess/include/data_process.h)。原有异步采样入口改名为 `dp_submit_sig_value()` / `dp_submit_sig_values()`，仍使用 host-endian、低 word 在前的寄存器数组，经队列和 DP 线程处理；ticket 等待机制保留。参数持久化和事务内派生输出分别使用各自入口。同步写入返回 0 或负 errno；直接读取成功返回容量。`dp_fast_para_get()` 还可能原样返回 READ 回调的非零状态，因此正数不一定是字节计数。新版错误值不保证与旧 MPP 数字一一对应，且同步接口只允许线程调用。

[collect_lib.h](../modules/collect/include/collect_lib.h) 保留所覆盖接口的名称和数值约定；原生采集调度继续使用 Zephyr 信号量、每总线串行访问、截止时间和有界队列。清队列只取消待执行请求，运行中的请求正常完成。参考应用在 `app/main.c:63–64` 另行声明的无参 `co_init()` / `tr_init()` 不在这四个头文件的统计内；原无参启动契约未覆盖，IDU 保留显式装配总线和 Modbus 接口的入口。

[transmit_lib.h](../modules/transmit/include/transmit_lib.h) 的 `TR_FUNC_CB` 有一项明确适配：接收 `const struct modbus_adu *request` 与 `struct modbus_adu *response`，不接收原始字节流。过滤器 0 继续、4 静默消费；模式处理器 -1 继续、0 使用 response 回复。原多帧过滤、CRC 前过滤、回调自行发送和 AES=2 语义没有被宣称兼容。SLEEP 不回复、不执行业务回调。原生更完整的错误和统计接口用 `tr_port_*` 命名，避免同名不同参数。旧端口控制仅 GET/CLR_RECV_MB 映射为进入分发的完整 ADU 计数，其他命令返回 `-ENOTSUP`，不拿软件统计冒充硬件 CRC/TX 计数。

## 点表的 Flash / RAM 分工

产品的 `app/cfg/tables/{process,collect,transmit,defaults}.c` 使用 `static const`。
同型号设备共享处理和采集行。`DEV_TBL_CFG_S.pst_table` 使用 `const void *`，
设备装配和少量总线描述留在 RAM；点行不复制为整张 native 数组。
启用 `CONFIG_DP_REFERENCE_TABLES` 后，`dp_config_point/collect/transmit()` 在调用者
提供的栈缓冲中构造当前条目的执行视图；指针不得保留到缓冲失效后。
参数索引、通知和默认覆盖分别保留索引或信号键，不保存临时视图指针。
默认/范围与周期回调会被反复调用，必须只读、确定，不执行 I/O 或改变注册配置。

测点当前值、质量、采样时间、NVS 缓存和事务暂存必须可写。运行状态的字段顺序已调整，
GD32E505 上每个 `stored_point` 从 32 字节缩至 24 字节；64 点容量节省 512 字节，
大点表配置的 560 点容量节省 4480 字节。动态修改默认值/范围使用
`CONFIG_DP_REFERENCE_OVERRIDES` 个稀疏覆盖槽（默认 4 个），不复制或改写整张 const 表。
覆盖只在本次运行有效，不持久化，也不立即改变当前值；满时返回 `-ENOSPC`。
读取大于 4 字节的默认值时，调用方须为 `def_value.pst` 提供配置长度的可写缓冲区。

新增服务也占用缓冲区，不能将上述状态数组节省量直接当作整机净节省量。采集直接写入
缓冲按 `min(CO_MAX_REGS, DP_POINT_WORDS)` 分配，通知队列长度通过
`CONFIG_DP_REFERENCE_EVENTS` 配置。最终固件占用见文末。

## 覆盖清单

按四个原头文件的 89 个不同名称统计，目前提供 **79 个同名入口**，比此前增加 39 个。
这是源码接口适配数量，不是完整行为或二进制兼容率。关闭相关 Kconfig 后，需对应机制的
接口会返回 `-ENOTSUP`；旧 void 接口失败会记录日志，不以空成功实现替代功能。

| 本次增加 | 数量 | 行为和限制 |
| --- | ---: | --- |
| DP 设备与协议表查询 | 10 | 原表返回只读指针；原生配置生成有界设备描述，包含本地测点设备和采集设备。没有原协议表时返回 NULL，不进行结构强转。按端口查询复制第一个匹配设备；缺失 port/code 返回 0，需先查设备判断是否存在。 |
| DP 默认值/范围、设备初始化及数据重置 | 4 | 稀疏覆盖保留 const；`dp_set_init` 先执行匹配设备的注册 INIT，再执行测点 INIT，失败返回错误，早先成功效果不回滚。重置仅接受已配置的远端设备，遥测回到初值并标为未初始化；已排队的旧采样失效，不重置本地、常量或持久化参数。 |
| DP 变化通知、参数登记和直接下发 | 4 | `dp_reg_sig_callback` 最多 3 个观察者；`dp_para_value_reg` 仅在回调内登记 OLD/NEW；注册采集函数用于 `dp_set_para_not_dm`，与 native 持久化传输分开。 |
| DP 延时存储刷新、重启和默认恢复 | 5 | 回调内刷新/重启标记仅在本次事务成功后执行；失败取消。默认恢复只选 RESTORE 参数；factory 额外刷写 NVS，不擦分区、不隐式重启。 |
| PLTCB 参数策略和通知 | 10 | 默认值策略、范围检查策略、长度迁移、参数日志及恢复完成的五组注册/调用接口；注册须在 DP 初始化前完成。 |
| CO 初始化回调、转换、直接写、RTU 注入、重启 | 5 | 使用 Zephyr 总线串行机制；具体边界如下。 |
| TR 连接状态配置 | 1 | 接收 ADU 的活动超时检测；状态通过扩展 `tr_port_connection_get` 查询，不代表电气连接状态。 |

新增 DP 查询为：`dp_dev_get_num/tbl/from_devid/from_portid/coll_addr/port/code`、
`dp_devid_get_prototbl`、`dp_sigcfg_get_by_devcfg/by_devid`。
具体实现位于 [reference_services.c](../modules/dataprocess/src/reference_services.c)、
[data_process.c](../modules/dataprocess/src/data_process.c)、
[collect.c](../modules/collect/src/collect.c) 和
[modbus_adapter.c](../modules/transmit/src/modbus_adapter.c)。

<a id="回调存储与直接下发的约定"></a>

### 回调、存储与直接下发的约定

- 变化通知和参数日志复制提交值后进入有界队列，由现有 DP 业务线程调用，避免持有数据池
  或缓存锁执行用户代码。队列满会丢通知并记录警告，不撤销已提交的数据；不是可靠审计日志。
  参数日志仅覆盖不超过 4 字节的变化，记录 Modbus 端口/地址；无 UTC 时 `date_time=0`，
  不伪造 UPP 地址。恢复完成通知仅在所选参数全部恢复成功后调用。
- 范围策略返回 false 可跳过数值上下限检查，仍保留类型容量和字节长度校验。
  默认策略返回 false 时采用回调填入的值；两类回调都须快速、不可重入 DP。
  初始化成功后冻结 PLTCB 注册，旧 void 注册函数不能报告错误，应用必须遵守启动顺序。
- NVS 长度迁移须在 `dp_equip_cfg.previous_parameter_schema` 中明确授权一个旧签名，
  并由迁移回调返回 true。只允许已识别的同设备/同信号/同类型条目转换长度，结果仍校验范围。
  未知签名、拒绝转换及损坏记录均报错且不格式化；未完成事务意图不走该长度迁移路径。
  持久化格式以 word 保存，不能据此检测奇数字节长度变化但 word 数不变的布局差异。
- `dp_para_reg_set_func` 注册一次，CO 初始化注册 `co_dp_fast_set`。直接下发复制请求数据，
  后端返回 0 表示接受且必须恰好完成一次，错误表示拒绝且不得随后完成。等待上限 1 秒；
  若已接受但超时，仍保留数据直到迟到完成，期间新请求返回 `-EBUSY`。同步下发接口不能
  从 ISR 调用，后端接受调用本身必须及时返回。原生持久化参数仍走带读回和恢复的 native
  transport；不将这个简化的完成回调套入 NVS 事务。
- `dp_set_para_not_dm` 成功后给当前事务已存在的匹配输出设置 NO_DM/NO_COLLECT；
  后续处理失败无法撤销已经到达外设的直接写入。`co_dp_fast_set` 发送前校验整批并复制转换值，
  使用参数显式转发地址或 FC3 采集映射，不把 FC4 输入寄存器推断为可写。写值转换回调返回
  0 使用转换值，其他返回值沿用原值；该回调仅作用于参考直接写路径。发送阶段后项失败时，
  先前成功的外设写入不会回滚，完成回调只在全部成功时调用一次。
- `co_anlysis_mb_pkg` 接收完整 RTU 响应（含单元号、功能码、字节数、CRC），支持 FC3/FC4；
  所有匹配字段须容纳在一个 DP batch，否则不入队。`reg_addr` 使用配置表地址，不暗加减 1。
  原应用 `PRE_COLLECT.C` 的请求地址减 1、解析时加 1 属于其产品地址约定，不搬到通用模块。
- `dp_flush_para_store` / `dp_set_reboot` 只能在处理回调中设置标记。`dp_reboot_system` 和
  `co_set_reboot_system` 通过 DP 保存后延时 100 ms 重启，不复刻旧平台的长时间阻塞，
  也不保证串口硬件已发送完毕；产品升级仍使用原有停机/发送完成流程。
- `dp_load_def_data` / `dp_reset_factory` 都遵循默认恢复策略，跨设备恢复可能在后项失败前
  已完成前项。`dp_para_value_reg` 的 OLD=0 / NEW=1 是明确的新定义：参考公开头未定义
  这两个常量，所检查库的此函数只是返回成功的桩；同库 `dp_load_def_data` 也为空桩。
  新实现提供实际功能，不声称从桩函数恢复出未知业务逻辑。
- `tr_port_conn_status_config` 启用后，依据完整 ADU 的活动时间和
  `CONFIG_TR_CONN_TIMEOUT_MS` 判定连接；SLEEP 为断开。没有旧产品告警点号自动映射。

### 仍未提供的 10 个入口

| 接口 | 原因 |
| --- | --- |
| `dp_dev_add`、`dp_dev_del`、`dp_reg_devchg_callback` | 当前采用启动后固定的 const 配置；动态增删还要处理运行中采集、索引、事务、NVS schema 和读者生命周期，不能只改设备数组。原库 add/del 的成功桩不作为实现。 |
| `dp_plat_ver_init` | 原实现写入固定本地设备/信号的版本字段，需确定产品点号与编码；不以 Zephyr 版本字符串替代产品版本协议。 |
| `dp_set_para_can_id`、`dp_updata_meter_coller_set` | 依赖 CAN 标识和电表采集产品策略，当前没有对应驱动配置/数据定义。 |
| `co_put_passthough_queue` | 原始透传包所有权、路由、应答关联与非 Modbus 协议未定义，不能直接塞进现有请求队列。 |
| `co_get_port_dev_bind_inf`、`co_set_dev_bind_status` | 依赖旧 DSP 设备发现/绑定模型，当前为静态 Zephyr 总线配置。 |
| `tr_aes_pkt_send` | 缺少旧帧格式、密钥/IV、认证与重放约定；仅有 AES 算法不足以兼容线协议。 |

原 `MB_TABLE_SEG_S` 原始分段回调、增量表、其他 TR 硬件控制命令以及 UPP 完整协议栈、
IEC104/61850、MQTT/FTP/WEB、EGD、CAN/PLC 产品业务、旧日志/文件/升级服务和 ARC
仍不在本次范围。已有 native 路由/分段/升级接口继续使用，不宣称兼容全部旧平台。

完整原接口原型、来源与导出对象见审计工具生成的 `audit.json` / `audit.txt`。
回归位于 [reference_services](../tests/reference_services)、[reference_config](../tests/reference_config)、
[reference_dp](../tests/reference_dp) 和 [reference_ports](../tests/reference_ports)。

<a id="验证记录2026-10-20"></a>

## 验证记录

2026-10-20 的回归、构建与 RAM 对比统一保存在[验证说明](testing.md#已记录验证2026-10-20)。
