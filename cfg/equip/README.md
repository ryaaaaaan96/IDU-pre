# 通讯点表配置

这里按参考工程 `MCUdemo/CODE/app/cfg/equip` 的方式组织设备与三表：
设备实例关联采集、处理和转发定义，具体业务由应用回调执行。
运行时使用本仓库的 Zephyr 模块，不依赖参考工程目录或预编译库。

## 文件职责

| 文件 | 修改内容 |
| --- | --- |
| [`../inc/app_sig_def.h`](../inc/app_sig_def.h) | 应用设备 ID、信号 ID；共同组成内部测点键 |
| [`../inc/dev_cfg.h`](../inc/dev_cfg.h) | `idu_equipment_config()` 与三表获取接口 |
| [`../inc/comm_cfg.h`](../inc/comm_cfg.h) | 协议回调、命令/状态寄存器和兼容窗口的配置契约 |
| [`../inc/aidc_cfg.h`](../inc/aidc_cfg.h) | AIDC 生成元数据的类型和获取接口 |
| [`dev_cfg.c`](dev_cfg.c)、[`instances.h`](instances.h) | 装配设备和三表；绑定设备、总线、从机 unit、北向窗口、周期及过期时间 |
| [`table/process.c`](table/process.c) | 装配本地点、型号点和可选 AIDC 处理表 |
| [`table/process_local.inc`](table/process_local.inc) | 本地信号、常量和演示参数，保持现有存储顺序 |
| [`table/process_mvio.h`](table/process_mvio.h) | MV-IO 型号的类型、宽度、范围和质量过期配置 |
| [`table/collect.c`](table/collect.c)、[`table/collect_mvio.h`](table/collect_mvio.h) | 采集块装配、南向功能码/地址、字段偏移、字序与周期 |
| [`table/transmit.c`](table/transmit.c) | 装配当前 profile 的北向映射 |
| [`table/transmit_local.inc`](table/transmit_local.inc)、[`table/transmit_mvio.h`](table/transmit_mvio.h) | 本地及型号信号的北向地址、别名与质量映射 |
| [`callback_function.c`](callback_function.c) | 选择 MV-IO 兼容路由或 AIDC 串口参数回调 |
| [`table/transmit_parameters.c`](table/transmit_parameters.c) | 控制命令的地址/触发值/动作绑定，以及参数状态寄存器 |
| [`table/transmit_mvio.c`](table/transmit_mvio.c) | 旧读写入口、标准扩展窗口、按顺序遍历的旧回调表 |

[`models/mvio.h`](models/mvio.h) 保留型号信号 ID，并聚合三个 `*_mvio.h`，
供现有模型使用者继续包含；三类宏分别展开为所属表的静态数据。
`IDU_MVIO_COLLECT_FIELDS` 只声明一份字段数组，各设备实例共享。
这对应参考工程多个设备引用同一型号点表的做法，实例值和调度状态仍各自独立。

处理表绑定的函数放在 [`src/process/`](../../src/process/)：
`point_callbacks.c` 负责电流换算和参数操作，`mvio_process.c` 负责旧保存地址
导入及校验，`aidc_process.c` 负责 AIDC 业务处理。
[`src/communication/`](../../src/communication/) 负责端口配置、报文解析/编码与
兼容路由，`ports/` 提供真实或虚拟采集后端。配置文件不创建线程或操作串口。

AIDC 的 [`aidc/aidc_cfg.c`](aidc/aidc_cfg.c) 提供 `models.inc`、`handlers.inc`
元数据；三表装配文件继续包含 `aidc/generated/` 对应片段。
串口参数转换在 `src/communication/aidc_serial.c`，模型模拟采集在
`src/communication/ports/aidc_virtual.c`。生成元数据不代表相关业务已实现，
本次分离保持原有支持范围与错误返回。

## 新增设备

同型号 MV-IO 设备按以下步骤增加：

1. 在 `cfg/inc/app_sig_def.h` 分配唯一逻辑设备 ID，在 `instances.h` 增加一行
   `IDU_MVIO_INSTANCES`：设备 ID、总线 ID、unit、FC04 基址、FC03 地址窗口、
   快速/身份周期、快速/身份过期时间。时间单位均为毫秒。
2. 确认相应总线已通过 `co_init_bus()` 注册后端；物理端口配置在
   `src/communication/ports/`。同总线不能重复 unit，不同总线可以相同。
   添加实例不会自动建立串口或虚拟从机。
3. 为北向选择不冲突的窗口。默认 MV-IO 兼容配置只显式开放现有扩展入口，
   **新窗口必须同时加入 `table/transmit_mvio.c` 的 `extensions[]`**。
   只增加实例/映射不保证请求能够进入普通转发处理。
4. 检查 `CONFIG_DP_MAX_DEVICES`、`CONFIG_DP_MAX_BUSES`、
   `CONFIG_DP_POINTS_MAX_POINTS` 和 `CONFIG_DP_POINTS_MAX_POLLS` 的容量，
   验证设备注册、采集、北向读取与掉线隔离；可参考 `tests/models` 的双实例用例。

新型号按 `process_<name>.h`、`collect_<name>.h`、`transmit_<name>.h` 的职责
建立声明，接入对应装配文件，并在 `dev_cfg.c` 注册实例。不要复制 MV-IO 的
协议地址、演示写地址或旧广播规则作为新设备协议。

## 新增测点或命令

1. 先确定内部 `(dev_id, sig_id)`、南向地址和北向地址，三者分别配置。
2. 本地定义放 `process_local.inc`，型号定义放所属 `process_<name>.h`；
   指定来源、类型、宽度、范围、初值及过期时间。需要处理时，在 `src/process/`
   实现函数并由表绑定；纯换算不得阻塞或递归访问引擎，事务业务使用已有
   `dp_process_ctx` / `dp_txn_*` 接口。
3. 采集点加入所属 `collect_<name>.h` 的字段及连续读块，明确宽度和字序；
   不跨设备未定义的寄存器空洞合帧。本地点同时修改对应采样发布方。
4. 在 `transmit_local.inc` 或所属 `transmit_<name>.h` 增加映射；新地址超出
   MV-IO 兼容 profile 的已有扩展入口时，同步修改 `extensions[]`。
5. 控制命令与诊断状态放 `transmit_parameters.c`，动作由 `src/process/`
   实现，解析仍由 `src/communication/parameter_protocol.c` 完成。
   命令表本身不新增持久化参数；需要保存的值应另建参数点。
6. 覆盖配置冲突、完整/部分长度、字序、错误质量及实际报文路由；新增持久点
   还需考虑 schema、旧快照和重启恢复。测试入口见
   [模块设计](../../docs/collect-process-transmit.md#验证入口)。

## 兼容约束

此次文件拆分保持点序、默认值、flags、字序和 NVS schema。后续增删或重排
持久点属于存储格式变更，不能把“目录重构不变”当成自动迁移能力。

`transmit_mvio.c` 中 `read_starts/write_starts` 控制请求起始地址，
`input/holding` 描述旧回调表的遍历顺序。读取会按条目顺序跨空洞推进，
不是地址连续递增；不能将它们排序或合并成普通连续地址段。保留版本字符串
整块读取、广播、unit 247、静默失败等既有路由规则。

当前标准扩展窗口是 10000–10023、31000–31008、31500–31502、31090–31093、
32000–32003 和 30460，仅从正常本机地址进入。10012 是虚拟模式标志，
10013 未定义；位于入口窗口中也不等于存在可读点。详细协议和历史证据见
[点表说明](../../docs/point-tables.md)，历史审计与当前文件位置分开记录。
