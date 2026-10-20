# 参考平台接口与静态逆向记录

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
| `app/main.c:101` 注册设备、默认/范围、定时表，再于 `:142` 起依次启动 DP、本地设备、CO、TR | [src/app/startup.c](../src/app/startup.c) 显式装配；[platform_callback.h](../modules/dataprocess/include/platform_callback.h) 保存配置注册契约 |
| `app/cfg/equip/dev_cfg.c:43` 的 `DEV_TABLE_S` 实例组合各协议表 | [src/cfg/devices](../src/cfg/devices) 的设备和型号配置 |
| `app/cfg/equip/table/{process,collect,transmit}.c` 分别描述数据、采集和寄存器映射 | [src/cfg/tables](../src/cfg/tables)，三表内容与处理执行分开 |
| `app/process` 的业务回调、`app/globalfunction.c` 的回调读写辅助 | [src/process](../src/process) 的业务处理，通用事务机制位于 DP 模块 |
| `app/port_filter`、`app/plc` 的报文处理和端口控制 | [src/communication](../src/communication) 的产品协议配置和 Zephyr 端口适配 |
| `platform/libs/plateform.lib` 的共享数据池、调度、采集、转发 | [modules/dataprocess](../modules/dataprocess)、[collect](../modules/collect)、[transmit](../modules/transmit) 的源码实现 |

参考工程的 `local_equip` 还包含产品功能。IDU 的采样映射、启动顺序和工程量换算继续属于应用。当前顺序为配置注册、`dp_init()`、采集接口、各总线 `co_init()`、参数恢复、`dp_process_start()`、`dp_reference_start()`、`co_start()`；TR 在地址就绪后绑定，产品依赖由应用显式安排。

## 配置契约与编译范围

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

`platform_callback.h:37–54` 的原注册关系为设备表 getter、数量 getter，以及 `PARA_GET_RANGE_DEF(dev_id, sig_id, DM_SIG_DATA_S *, bool *recover_default)`。新版通过 `pltcb_reg_cfg_get_dev_tbl()`、`pltcb_reg_cfg_get_dev_num()`、`pltcb_reg_para_range_def_cb()` 注册，在 `dp_init(void)` 中使用有界静态数组编译，成功初始化后冻结注册。实现见 [reference_config.c](../modules/dataprocess/src/reference_config.c)。

启用 `CONFIG_DP_REFERENCE_TABLES` 后，编译器接收 U8、U16、S16、U32、S32、STRING、U8ARRAY、U16ARRAY，以及 `PRO_DATA_PROCESS`、简单的 Modbus MASTER/SLAVE 表。字节长度须精确匹配类型及配置容量。持久化本地点需要 `CONFIG_DP_PARAMS`，NVS 分区及恢复仍由应用启动。默认/范围来自注册回调；未注册时初值为零并使用类型范围。回调给出的最小值和最大值均为零时，表示不额外检查范围，仍保留类型的完整取值范围，不将值限制为零。持久化点要求强制恢复默认值时会拒绝配置，避免静默覆盖已有 NVS 数据。

原 `rqst_cyl` 的头文件只写“查询频率”。反汇编 `co_port_mgr.o:co_porc_scan_list+0x70 / +0x112 / +0x11a` 可见计数递减和重装，不能直接当作毫秒。当前要求显式注册 `dp_register_collect_period()`，由产品将该编码转换为毫秒，零表示仅初始化读取。帧合并、特殊采集数据类型、`scan_pri`、`FLAG_COLLECT` 和未支持协议会使初始化失败，不会被忽略。

Modbus 字序采用 `arc_byte_order.h:60–64` 的实际宏：0 默认按 BIGSMALL、1 SMALL、2 BIGSMALL、3 SMALLBIG、4 BIG。0/2 对应 native 低字在前，4 对应高字在前，1/3 另需字节交换。UPP 的 `PUB_BYTE_ORDER_BIG=0` 是另一套编码，不能混用；无需引入该头里的 ARC 函数。

现有 IDU 表含产品迁移钩子和采集模型，继续通过显式扩展 `dp_register_config_provider(idu_equipment_config)` 使用，不伪装成 `DEV_TABLE_S`。该入口与原表 getter 互斥，底层 native 初始化入口是 `dp_init_config(cfg)`。

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

## 覆盖清单

下表描述源码入口与适配范围，不表示硬件验证或完整回归结果。

| 范围 | 实现位置 / 限制 |
| --- | --- |
| 原配置注册、设备三表编译 | `modules/dataprocess/src/reference_config.c`；仅上述显式支持子集 |
| 同步 byte set/get、fast set/get、事务回调辅助 | [data_process.c](../modules/dataprocess/src/data_process.c)；事务执行需要 `CONFIG_DP_PROCESS`，类型、容量和持久化受 native 配置约束 |
| 原 CO 模式、设备初始化、清队列、周期扫描开关 | `modules/collect/src/collect.c`；五个接口，OTHER 不支持 |
| 原 TR 模式、全局处理器、每端口过滤器、有限控制 | `modules/transmit/src/modbus_adapter.c`；六个接口，回调使用 Zephyr ADU |
| 原全局 INIT/TIMER 表注册 | `reference_config.c` 的 `dp_reference_start()`；INIT 按表序，TIMER 仅周期 type=0；native `dp_process_plan` 仍可独立使用 |

以下原公开能力不在上述覆盖范围内，不提供返回成功的空实现：

- DP 设备动态增删/变化通知和旧设备查询族、信号变化通知、参数注册/转发器注册、恢复出厂/载入默认、旧定时和 NO_DM 设置、默认范围动态修改、旧重启/平台版本/CAN 标识/电表采集配置/设备数据重置接口。原库 `dp_dev_add()` / `dp_dev_del()` 本身只是成功返回的桩，这不构成新版应提供相同空行为的理由。
- PLTCB 的按点决定载入默认值、动态范围检查、长度不匹配迁移、参数日志和恢复结束通知；IDU 现有迁移功能走明确的 native 配置，不等于实现这些旧入口。
- CO 的原始透传包队列、`co_dp_fast_set()`、每端口初始化回调注册、旧重启接口、原始 Modbus 包解析注入、写值转换注册、DSP 设备绑定查询/设置。已有 native 功能不能据此视为同名原接口可直接调用。
- TR 的旧连接状态配置、AES 发包、原 `MB_TABLE_SEG_S` 原始分段回调及增量表；产品现有路由/分段配置继续使用 native 接口。
- 范围外子系统包括 UPP 完整协议栈、IEC104/61850、MQTT/FTP/WEB、EGD、CAN/PLC 专用业务、旧日志/文件/升级服务，以及 ARC OS/HAL。保留某些协议枚举用于识别和拒绝，不代表实现了对应协议。

按四个原头文件的 89 个名称统计，当前提供 40 个同名入口，适配限制如上。其余 49 个未提供的入口如下，不能凭功能相近就按原签名调用：

```text
DP:
dp_dev_add, dp_dev_del, dp_dev_get_code, dp_dev_get_coll_addr,
dp_dev_get_from_devid, dp_dev_get_from_portid, dp_dev_get_num,
dp_dev_get_port, dp_dev_get_tbl, dp_devid_get_prototbl,
dp_dm_reset_dev_data, dp_flush_para_store, dp_get_para_def_range,
dp_load_def_data, dp_para_reg_set_func, dp_para_value_reg,
dp_plat_ver_init, dp_reboot_system, dp_reg_devchg_callback,
dp_reg_sig_callback, dp_reset_factory, dp_set_init,
dp_set_para_can_id, dp_set_para_def_range, dp_set_para_not_dm,
dp_set_reboot, dp_sigcfg_get_by_devcfg, dp_sigcfg_get_by_devid,
dp_updata_meter_coller_set

CO:
co_anlysis_mb_pkg, co_dp_fast_set, co_get_port_dev_bind_inf,
co_init_func_reg, co_put_passthough_queue, co_set_dev_bind_status,
co_set_reboot_system, co_set_value_trans_cb_reg

TR:
tr_aes_pkt_send, tr_port_conn_status_config

PLTCB:
pltcb_para_get_is_load_def, pltcb_para_get_is_range_check,
pltcb_para_recover_fininsh, pltcb_para_size_mismatch_pro, pltcb_para_write_log,
pltcb_reg_para_is_load_def_cb, pltcb_reg_para_is_range_check,
pltcb_reg_para_recover_finish_cb, pltcb_reg_para_size_mismatch_cb,
pltcb_reg_para_write_log_cb
```

完整原接口原型、来源行与导出对象见工具生成的 `audit.json` / `audit.txt`。对应边界用例位于 [reference_config](../tests/reference_config)、[reference_dp](../tests/reference_dp)、[reference_ports](../tests/reference_ports)。新增覆盖应同时更新源码、边界测试和本文，不能只补声明。

## 本次验证（2026-10-20）

工作区根目录执行以下命令均通过，输出位于 `build/IDU-pre/tests/reference-*.log`：

```bash
CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --iapv3
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --aidc
```

新增参考接口用例共 22 项：DP 10 项、关闭 DP_PROCESS 后 3 项、CO/TR 8 项、
配置与 INIT/TIMER 集成 1 项。完整测试入口通过；旧 QuickLZ 对照与外部 SGZ
样本相关的 3 项因未提供 `IDU_LEGACY_IAP_DIR` / `IDU_IAP_SGZ` 而跳过。
ASan/UBSan 保留，当前执行环境禁用 LeakSanitizer。

另外导出暂存内容后独立构建默认固件，确认提交不依赖工作区其他未提交修改。
三种产品构建的静态 RAM 占比分别约为 61.44%、86.46%、91.31%；最后一项为
大点表配置，实板运行时的栈余量、采集时序和通信表现仍需验证。本次未进行实板测试。
