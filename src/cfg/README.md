# 通讯点表配置

参考工程 `MCUdemo/CODE/app/cfg/equip` 用设备实例关联采集、处理和转发表。
这里保留这一关系，源码和头文件直接放在 `src/cfg/`，本地条目直接写在对应
`.c` 中。运行时使用本仓库的 Zephyr 模块，不依赖参考工程目录或预编译库。

| 文件 | 职责 |
| --- | --- |
| [`signal_ids.h`](signal_ids.h) | 应用设备 ID、信号 ID，共同组成内部测点键 |
| [`device_config.c`](device_config.c)、[`device_config.h`](device_config.h)、[`instances.h`](instances.h) | 装配设备和三表；绑定总线、unit、北向窗口、周期及过期时间 |
| [`process.c`](process.c) | 信号类型、宽度、范围、默认值、存储属性及处理绑定；包含本地点和参数 |
| [`collect.c`](collect.c) | 采集块、字段、南向地址、字序和周期 |
| [`transmit.c`](transmit.c) | 北向寄存器到信号的映射，包含本地条目 |
| [`mvio_model.h`](mvio_model.h) | MV-IO 信号 ID 和采集、处理、转发三组复用宏，多实例共享采集字段 |
| [`point_metadata.c`](point_metadata.c)、[`point_metadata.h`](point_metadata.h) | 通过 `idu_collect_models()`、`idu_process_bindings()` 提供采集模型和处理绑定 |
| [`generated/symbols.h`](generated/symbols.h)、[`generated/migration.json`](generated/migration.json) | 导入符号与来源、诊断记录；这是唯一保留的子目录 |

业务回调在 [`src/process/`](../process/) 执行。端口和报文处理在
[`src/communication/`](../communication/)，协议路由、控制命令及兼容窗口在
[`src/communication/config/`](../communication/config/)，应用选项在
[`src/Kconfig`](../Kconfig)。点表只声明数据及绑定，不创建线程或操作串口。

## 修改设备或测点

1. 在 `signal_ids.h` 分配应用 ID；同型号 MV-IO 实例直接加入 `instances.h`。
   每行配置设备、总线、unit、北向窗口、采集周期和过期时间。新总线还需在
   通信层注册 `co_init_bus()` 后端；添加实例不会自动建立串口或虚拟从机。
2. 本地点和参数在 `process.c` 修改，MV-IO 型号点在 `mvio_model.h` 的处理
   定义中修改。需要业务计算时，在 `src/process/` 实现函数并由点表绑定。
3. 同步调整 `collect.c` 或型号的采集宏，明确字段宽度、地址和字序；不要
   跨未定义的寄存器空洞合帧。本地采样点还需修改对应发布方。
4. 在 `transmit.c` 或型号的转发宏中增加映射。**新增北向窗口还需同步调整
   `src/communication/config/mvio_config.c` 的 `extensions[]`**；只有映射
   不保证请求能进入点表。控制命令放通信层的 `parameter_config.c`。
5. 检查设备、总线、测点和采集块容量，验证宽度、字序、质量、路由和设备隔离。
   新增持久点还需考虑 NVS schema、旧快照及重启恢复；测试入口见
   [模块设计](../../docs/collect-process-transmit.md#验证入口)。

其他型号可先直接在对应 `.c` 中声明三表；需要多个实例共享定义时，再参考
`mvio_model.h`。型号地址、演示参数和旧广播规则应按实际设备协议配置。

## 更新导入点表

导入数据直接位于 `.c` 的标记区段中：`process.c` 的 `points`，`transmit.c`
的 `transmit`，`collect.c` 的 `fields/collect`，`device_config.c` 的 `devices`，
以及 `point_metadata.c` 的 `models/handlers`。区段由
`/* BEGIN IMPORTED TABLE: points */` 与 `/* END IMPORTED TABLE: points */`
这样的成对注释界定；重新导入会更新区段内容，保留标记外的手写配置。

从工作区运行：

```bash
python applications/IDU-pre/tools/import_aidc.py --source /path/to/AIDC_CODE/CODE
```

`--output` 默认是 `src/cfg`；工具同步更新上述区段和 `generated/` 中的两个
文件。处理绑定的 `callback` 是原回调名称字符串，实际执行仍在 `src/process/`。
现有 AIDC 来源、版权注释、`CONFIG_IDU_AIDC_PROFILE`、生成宏和业务函数名
保持兼容，生成元数据本身不代表相关业务已实现。

此次整理保持点序、默认值、flags、字序和 NVS schema。MV-IO 旧回调按条目
顺序跨地址空洞读取的规则仍在通信层维护，不能将其排序为普通连续地址段。
协议细节与历史证据见[点表说明](../../docs/point-tables.md)。
