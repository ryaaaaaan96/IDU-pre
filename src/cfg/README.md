# 通讯点表配置

这里按参考工程 `MCUdemo/CODE/app/cfg/equip` 的方式组织设备与三表：
设备实例关联采集、处理和转发定义，具体业务由应用回调执行。
`src/cfg/` 只保留点表相关内容，按 `include`、`equipment`、`tables`、`aidc`
分类：信号定义、设备装配、采集描述、处理绑定、寄存器映射及生成元数据。
下列相对路径以本目录为准。
运行时使用本仓库的 Zephyr 模块，不依赖参考工程目录或预编译库。

## 文件职责

| 文件 | 修改内容 |
| --- | --- |
| [`include/signal_ids.h`](include/signal_ids.h) | 应用设备 ID、信号 ID；共同组成内部测点键 |
| [`include/device_config.h`](include/device_config.h) | `idu_equipment_config()` 与三表获取接口 |
| [`include/aidc_config.h`](include/aidc_config.h) | AIDC 生成元数据的类型和获取接口 |
| [`equipment/device_config.c`](equipment/device_config.c)、[`equipment/instances.h`](equipment/instances.h) | 装配设备和三表；绑定设备、总线、从机 unit、北向窗口、周期及过期时间 |
| [`tables/process.c`](tables/process.c) | 装配本地点、型号点和可选 AIDC 处理表 |
| [`tables/process_local.inc`](tables/process_local.inc) | 本地信号、常量和演示参数，保持现有存储顺序 |
| [`tables/process_mvio.h`](tables/process_mvio.h) | MV-IO 型号的类型、宽度、范围和质量过期配置 |
| [`tables/collect.c`](tables/collect.c)、[`tables/collect_mvio.h`](tables/collect_mvio.h) | 采集块装配、南向功能码/地址、字段偏移、字序与周期 |
| [`tables/transmit.c`](tables/transmit.c) | 装配当前 profile 的北向映射 |
| [`tables/transmit_local.inc`](tables/transmit_local.inc)、[`tables/transmit_mvio.h`](tables/transmit_mvio.h) | 本地及型号信号的北向地址、别名与质量映射 |
| [`aidc/aidc_config.c`](aidc/aidc_config.c)、[`aidc/generated/`](aidc/generated/) | AIDC 三表片段、信号采集模型及处理绑定元数据 |

[`equipment/models/mvio.h`](equipment/models/mvio.h) 保留型号信号 ID，并聚合三个 `*_mvio.h`，
供现有模型使用者继续包含；三类宏分别展开为所属表的静态数据。
`IDU_MVIO_COLLECT_FIELDS` 只声明一份字段数组，各设备实例共享。
这对应参考工程多个设备引用同一型号点表的做法，实例值和调度状态仍各自独立。

处理表绑定的函数放在 [`src/process/`](../process/)：
`point_callbacks.c` 负责电流换算和参数操作，`mvio_process.c` 负责旧保存地址
导入及校验，`aidc_process.c` 负责 AIDC 业务处理。
[`src/communication/`](../communication/) 负责端口配置、报文解析/编码，
`ports/` 提供真实或虚拟采集后端。点表配置文件不创建线程或操作串口。

协议回调选择、路由窗口和控制命令在
[`src/communication/config/`](../communication/config/)；它们控制请求如何进入
点表或触发动作，点表的 `transmit` 映射则描述寄存器对应哪个信号。
应用功能选项统一在 [`src/Kconfig`](../Kconfig)，符号与默认值保持不变。

AIDC 的 `models.inc` 描述信号采集模型，`handlers.inc` 描述信号处理绑定，
由 `aidc/aidc_config.c` 提供获取接口；它们属于点表元数据，继续保留在此。
三表装配文件包含 `aidc/generated/` 对应片段，实际业务在 `src/process/` 执行。
串口参数转换在 `src/communication/aidc_serial.c`，模型模拟采集在
`src/communication/ports/aidc_virtual.c`。生成元数据不代表相关业务已实现，
配置迁移和文件重命名保持原有支持范围与错误返回。

## 新增设备

同型号 MV-IO 设备按以下步骤增加：

1. 在 `src/cfg/include/signal_ids.h` 分配唯一逻辑设备 ID，在 `equipment/instances.h` 增加一行
   `IDU_MVIO_INSTANCES`：设备 ID、总线 ID、unit、FC04 基址、FC03 地址窗口、
   快速/身份周期、快速/身份过期时间。时间单位均为毫秒。
2. 确认相应总线已通过 `co_init_bus()` 注册后端；物理端口配置在
   `src/communication/ports/`。同总线不能重复 unit，不同总线可以相同。
   添加实例不会自动建立串口或虚拟从机。
3. 为北向选择不冲突的窗口。默认 MV-IO 兼容配置只显式开放现有扩展入口，
   **新窗口必须同时加入 `src/communication/config/mvio_config.c` 的 `extensions[]`**。
   只增加实例/映射不保证请求能够进入普通转发处理。
4. 检查 `CONFIG_DP_MAX_DEVICES`、`CONFIG_DP_MAX_BUSES`、
   `CONFIG_DP_POINTS_MAX_POINTS` 和 `CONFIG_DP_POINTS_MAX_POLLS` 的容量，
   验证设备注册、采集、北向读取与掉线隔离；可参考 `tests/models` 的双实例用例。

新型号在 `tables/` 按 `process_<name>.h`、`collect_<name>.h`、
`transmit_<name>.h` 的职责建立声明，接入对应装配文件，并在 `equipment/device_config.c` 注册实例。不要复制 MV-IO 的
协议地址、演示写地址或旧广播规则作为新设备协议。

## 新增测点

1. 先确定内部 `(dev_id, sig_id)`、南向地址和北向地址，三者分别配置。
2. 本地定义放 `tables/process_local.inc`，型号定义放所属 `tables/process_<name>.h`；
   指定来源、类型、宽度、范围、初值及过期时间。需要处理时，在 `src/process/`
   实现函数并由表绑定；纯换算不得阻塞或递归访问引擎，事务业务使用已有
   `dp_process_ctx` / `dp_txn_*` 接口。
3. 采集点加入所属 `tables/collect_<name>.h` 的字段及连续读块，明确宽度和字序；
   不跨设备未定义的寄存器空洞合帧。本地点同时修改对应采样发布方。
4. 在 `tables/transmit_local.inc` 或所属 `tables/transmit_<name>.h` 增加映射；新地址超出
   MV-IO 兼容 profile 的已有扩展入口时，同步修改 `extensions[]`。
5. 覆盖配置冲突、完整/部分长度、字序、错误质量及实际报文路由；新增持久点
   还需考虑 schema、旧快照和重启恢复。测试入口见
   [模块设计](../../docs/collect-process-transmit.md#验证入口)。

恢复默认、刷新存储等控制命令在
[`src/communication/config/parameter_config.c`](../communication/config/parameter_config.c)
配置，由通信层解析并调用业务动作。命令表本身不新增持久化参数；需要保存的
值应另建参数点。

## 兼容约束

此次文件拆分保持点序、默认值、flags、字序和 NVS schema。后续增删或重排
持久点属于存储格式变更，不能把“目录重构不变”当成自动迁移能力。

通信层保留 MV-IO 旧回调按条目顺序跨地址空洞读取的语义；它与本目录的普通
寄存器映射分别维护，不能将旧回调表排序或合并成连续地址段。扩展窗口仍需在
通信配置中显式开放；进入窗口也不等于存在可读点。详细协议和历史证据见
[点表说明](../../docs/point-tables.md)，历史审计与当前文件位置分开记录。
