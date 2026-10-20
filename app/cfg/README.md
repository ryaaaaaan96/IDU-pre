# 通讯点表配置

[文档导航](../../docs/README.md) · [点表与寄存器](../../docs/point-tables.md)

`device.c` 只保留产品设备关系和注册函数；三表校验、执行视图及数据池初始化
均由 `modules/dataprocess` 完成。

点表沿用参考工程的结构：设备表关联处理、采集和转发表，业务通过信号 ID 和
函数指针连接。所有点行及默认值为 `const`，多个设备共享型号表；DP 按需读取，
不在 RAM 中复制整套点表。不引入 ARC，也不链接参考平台的预编译库。

```text
cfg/
├── device.c              # 设备关联及配置回调注册
├── inc/                  # 本目录全部配置头文件
│   ├── device.h
│   ├── device_instances.h
│   ├── mvio_signals.h
│   ├── signal_ids.h
│   └── imported_signals.h
└── tables/               # 纯数据表
    ├── process.c
    ├── collect.c
    ├── transmit.c
    └── defaults.c
```

| 文件 | 内容 |
| --- | --- |
| `device.c`、`inc/device.h` | `DEV_TABLE_S` 装配及 `pltcb_*` 注册，周期编码转毫秒 |
| `inc/device_instances.h` | MV-IO 设备 ID、总线、unit、北向窗口和地址寄存器 |
| `inc/mvio_signals.h` | MV-IO 信号枚举和采集周期编码 |
| `tables/process.c` | `DM_SIG_CFG_S`：信号 ID、字节数、标志、类型、回调 |
| `tables/collect.c` | `MODBUS_RTU_TABLE_S`；可选 UPP 模型使用 `UPP_M_MOD_DATA / UPP_M_MOD / UPP_M_MOD_REG` |
| `tables/transmit.c` | `MB_TABLE_CELL_T`：寄存器地址、长度、设备/信号和字序 |
| `tables/defaults.c` | 原接口 `DM_SIG_DATA_S` 所需默认值、范围及恢复默认策略 |
| `inc/signal_ids.h`、`inc/imported_signals.h` | 设备和信号宏，分别维护应用定义和导入定义 |

`process.c` 直接绑定 `app/process/` 的回调，不再维护 `point_metadata`、回调名称
字符串或另一套 UPP 模型/初值副本。虚拟模型后端直接读取 `collect.c` 和 `defaults.c`。
`defaults.c` 是参考范围回调的数据来源，不是 NVS 迁移配置。

初始化由 `app/process/points.c` 调用 `idu_register_point_tables()`、`dp_init()`、总线初始化、参数恢复和
处理/采集启动。原始采样到信号的映射及工程量换算留在业务层，硬件模块只返回样本。
参考结构与接口边界见[参考平台对照](../../docs/reference-platform.md)。

## 修改设备或测点

1. 用宏或枚举命名信号 ID，在 `process.c` 添加原格式行，需要处理时直接指定回调。
2. 在 `defaults.c` 配置默认值和范围，通过 `pltcb_reg_para_range_def_cb()` 提供给 DP。
   本地恒定标识使用 `idu_default_point` 初始化；采集点等待首次样本。
3. 在 `collect.c` 定义协议地址、长度、字序和周期编码。当前每个 Modbus 行独立请求，
   不跨未定义空洞合帧；`rqst_cyl` 经设备配置中的转换函数解释，不直接当毫秒。
4. 在 `transmit.c` 增加北向映射。新窗口也需加入
   `app/communication/config/mvio_config.c` 的 `extensions[]`，控制命令留在通信配置。
5. 同型号 MV-IO 实例加入 `device_instances.h`，复用处理/采集表；新总线仍须注册通信后端。
   检查 Kconfig 的测点、值池、采集任务和参数容量。持久点改变会影响 NVS schema。

质量寄存器映射、按样本年龄过期、旧参数导入及 NVS schema 自动迁移暂不接入应用。
待办与旧存储不匹配时的行为见[根 README](../../README.md#点表暂缓的扩展)。
通信请求超时、内部错误质量和基本参数保存/恢复继续有效。

## 更新导入点表

`tools/import_aidc.py` 更新 `.c` 中的成对标记区，保留标记外的手写内容：

- `process.c`：`points`；`defaults.c`：`defaults`。
- `collect.c`：`fields`、`collect`；`transmit.c`：`transmit`。
- `device.c`：`devices`。

从工作区执行：

```bash
python applications/IDU-pre/tools/import_aidc.py --source /path/to/AIDC_CODE/CODE
```

默认输出为 `app/cfg`，信号宏写入 `inc/imported_signals.h`，来源及诊断写入
[`docs/point-table-import.json`](../../docs/point-table-import.json)。自定义 `--output`
的报告默认为 `<output>/migration.json`，可用 `--report` 指定。没有 `.inc` 或
`generated/` 运行代码。报告中的原回调名称用于追溯；工具将它转换为明确的函数指针，
未实现的业务写操作绑定 `idu_unsupported_service` 并返回错误。

原数据中类型与字节数不一致的条目保留诊断，显式转为字数组；缺少处理定义的映射
保持未初始化，不能把补零当成有效设备值。UPP 仅接入模型表和虚拟后端，未实现线协议。
