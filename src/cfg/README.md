# 通讯点表配置

[文档导航](../../docs/README.md) · [点表与寄存器](../../docs/point-tables.md)

参考工程 `MCUdemo/CODE/app/cfg/equip` 用设备实例关联采集、处理和转发表。
这里保留这一关系，`src/cfg/devices/` 管设备与型号，`src/cfg/tables/` 管三表
及信号元数据。本地条目和导入数据直接写在对应 `.c` 中。
运行时使用本仓库的 Zephyr 模块，不依赖参考工程目录或预编译库。

| 文件 | 职责 |
| --- | --- |
| [`tables/signal_ids.h`](tables/signal_ids.h) | 应用设备 ID、信号 ID，共同组成内部测点键 |
| [`devices/device_config.c`](devices/device_config.c)、[`devices/device_config.h`](devices/device_config.h)、[`devices/instances.h`](devices/instances.h) | 装配设备和三表；绑定总线、unit、北向窗口、周期及过期时间 |
| [`tables/process.c`](tables/process.c) | 信号类型、宽度、范围、默认值、存储属性及处理绑定；包含本地点和参数 |
| [`tables/collect.c`](tables/collect.c) | 采集块、字段、南向地址、字序和周期 |
| [`tables/transmit.c`](tables/transmit.c) | 北向寄存器到信号的映射，包含本地条目 |
| [`devices/mvio_model.h`](devices/mvio_model.h) | MV-IO 信号 ID 和采集、处理、转发三组复用宏，多实例共享采集字段 |
| [`tables/point_metadata.c`](tables/point_metadata.c)、[`tables/point_metadata.h`](tables/point_metadata.h) | 通过 `idu_collect_models()`、`idu_process_bindings()` 提供采集模型和处理绑定 |
| [`tables/imported_signals.h`](tables/imported_signals.h) | 导入点表使用的信号符号 |

业务回调在 [`src/process/`](../process/) 执行。端口和报文处理在
[`src/communication/`](../communication/)，协议路由、控制命令及兼容窗口在
[`src/communication/config/`](../communication/config/)，应用选项在
[`src/Kconfig`](../Kconfig)。点表只声明数据及绑定，不创建线程或操作串口。
[`src/app/startup.c`](../app/startup.c) 装配初始化，
[`src/app/point_service.c`](../app/point_service.c) 负责采样映射，
[`src/process/engineering.c`](../process/engineering.c) 负责工程值换算。
应用接口在 [`idu_point_service.h`](../../include/idu_point_service.h)，保留
`idu_equipment_init()` 名称。点表机制由 `dataprocess`、`collect`、`transmit`
提供；独立的 `quicklz` 只负责解码，与点表配置无关。

业务头文件及原格式 `DEV_TABLE_S` 接入方式见
[参考平台对照](../../docs/reference-platform.md)。IDU 当前使用
`dp_register_config_provider(idu_equipment_config)` 注册现有三表，再调用
`dp_init()`；这样保留 NVS 迁移钩子、明确的采集周期及协议模型。
不把现有配置强转为旧结构，也不引入 ARC 接口。

## 修改设备或测点

1. 在 `tables/signal_ids.h` 分配应用 ID；同型号 MV-IO 实例直接加入 `devices/instances.h`。
   每行配置设备、总线、unit、北向窗口、采集周期和过期时间。新总线还需在
   通信层注册 `co_init_bus()` 后端；添加实例不会自动建立串口或虚拟从机。
2. 本地点和参数在 `tables/process.c` 修改，MV-IO 型号点在 `devices/mvio_model.h` 的处理
   定义中修改。需要业务计算时，在 `src/process/` 实现函数并由点表绑定。
3. 同步调整 `tables/collect.c` 或型号的采集宏，明确字段宽度、地址和字序；不要
   跨未定义的寄存器空洞合帧。本地采样点还需修改对应发布方。
4. 在 `tables/transmit.c` 或型号的转发宏中增加映射。**新增北向窗口还需同步调整
   `src/communication/config/mvio_config.c` 的 `extensions[]`**；只有映射
   不保证请求能进入点表。控制命令放通信层的 `parameter_config.c`。
5. 检查设备、总线、测点和采集块容量，验证宽度、字序、质量、路由和设备隔离。
   新增持久点还需考虑 NVS schema、旧快照及重启恢复；测试入口见
   [验证说明](../../docs/testing.md#覆盖入口)。

其他型号可先直接在对应 `.c` 中声明三表；需要多个实例共享定义时，再参考
`devices/mvio_model.h`。型号地址、演示参数和旧广播规则应按实际设备协议配置。

## 更新导入点表

导入数据直接位于 `.c` 的标记区段中：`tables/process.c` 的 `points`，`tables/transmit.c`
的 `transmit`，`tables/collect.c` 的 `fields/collect`，`devices/device_config.c` 的 `devices`，
以及 `tables/point_metadata.c` 的 `models/handlers`。区段由
`/* BEGIN IMPORTED TABLE: points */` 与 `/* END IMPORTED TABLE: points */`
这样的成对注释界定；重新导入会更新区段内容，保留标记外的手写配置。

从工作区运行：

```bash
python applications/IDU-pre/tools/import_aidc.py --source /path/to/AIDC_CODE/CODE
```

`--output` 默认是 `src/cfg`，工具更新上述区段与 `tables/imported_signals.h`。
默认仓库输出的来源、诊断报告在 [`docs/point-table-import.json`](../../docs/point-table-import.json)；
自定义 `--output` 时报告默认放 `<output>/migration.json`，可用 `--report` 指定。
处理绑定的 `callback` 是原回调名称字符串，实际执行仍在 `src/process/`。
现有 AIDC 来源、版权注释、`CONFIG_IDU_AIDC_PROFILE`、生成宏和业务函数名
保持兼容，生成元数据本身不代表相关业务已实现。

调整目录时应保持点序、默认值、flags、字序和 NVS schema。MV-IO 旧回调按条目
顺序跨地址空洞读取的规则仍在通信层维护，不能将其排序为普通连续地址段。
协议细节与历史证据见[点表说明](../../docs/point-tables.md)。
