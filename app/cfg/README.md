# 点表与模块配置

[文档导航](../../docs/README.md) · [点表与寄存器](../../docs/point-tables.md)

`device.c` 声明产品设备与点表的关联并注册回调；三表校验、执行视图及数据池初始化
均由 `modules/dataprocess` 完成。
端口、协议和仿真配置也集中在本目录；只有 const 数据和实例声明，通信执行代码
归 `collect` / `transmit`，不在应用中重新实现。详见[通信端口接入](../../docs/communication-ports.md)。

点表沿用参考工程的结构：设备表关联处理、采集和转发表，业务通过信号 ID 和
函数指针连接。设备表、点行及默认值均为 `const`，多个设备共享型号表；DP 按需读取，
不在 RAM 中复制整套点表。不引入 ARC，也不链接参考平台的预编译库。

```text
cfg/
├── device.c              # 设备关联及配置回调注册
├── ports.c               # 传输链路、虚拟端口和采集口配置
├── forwarding.c          # 转发协议和串口配置
├── parameters.c          # 参数状态/命令寄存器绑定
├── mvio.c                # MV-IO 兼容范围和升级回调
├── simulation.c          # 可选模拟设备/模型配置
├── inc/                  # 本目录全部配置头文件
│   ├── ports.h            # 各模块产品配置声明
│   ├── device.h
│   ├── tables.h           # 点表声明及编译期数量
│   ├── mvio_signals.h
│   ├── signal_ids.h
│   └── imported_signals.h
└── tables/               # 纯数据表
    ├── process.c
    ├── collect.c
    └── transmit.c
```

| 文件 | 内容 |
| --- | --- |
| `device.c`、`inc/device.h` | 完整 `DEV_TABLE_S` 配置及 `pltcb_*` 注册，周期编码转毫秒 |
| `inc/tables.h` | `extern const` 点表声明、手写及导入点表的数量枚举 |
| `inc/mvio_signals.h` | MV-IO 信号枚举和采集周期编码 |
| `tables/process.c` | `DM_SIG_CFG_S` 信号定义；`parameters[]` 集中默认值、范围及恢复策略 |
| `tables/collect.c` | `MODBUS_RTU_TABLE_S`；可选 UPP 模型使用 `UPP_M_MOD_DATA / UPP_M_MOD / UPP_M_MOD_REG` |
| `tables/transmit.c` | `MB_TABLE_CELL_T`：寄存器地址、长度、设备/信号和字序 |
| `inc/signal_ids.h`、`inc/imported_signals.h` | 设备和信号宏，分别维护应用定义和导入定义 |

`process.c` 直接绑定 `app/process/` 的回调，不再维护 `point_metadata`、回调名称
字符串或另一套 UPP 模型/初值副本。虚拟模型后端直接读取 `collect.c` 和 `process.c`。
默认值和范围通过原接口 `DM_SIG_DATA_S` 返回，不改变参考结构。
处理、采集和转发表的行直接使用结构体字段初始化，信号 ID、标志和字序保留命名常量。
`transmit.c` 逐项列出实际寄存器地址，北向窗口不再通过设备实例宏自动生成。
三表由 CMake 独立编译，通过 `inc/tables.h` 的 `extern const` 声明供设备表引用，
`device.c` 不包含 `.c` 文件。设备关联仍为编译期常量，没有运行时装配。
数量使用枚举常量：C 的 `extern const` 数量变量不能用于静态结构体初始化，
不完整数组的声明也不能直接用于 `ARRAY_SIZE`。
每张表在定义处使用 `BUILD_ASSERT(ARRAY_SIZE(...) == ..._COUNT)` 校验；
声明和定义都不指定数组长度，避免缺少初始化项时被自动补零。
手写点表增删条目需同步调整枚举，导入点表的数量由导入工具同步生成。

初始化由 `app/process/points.c` 调用 `idu_register_point_tables()`、`dp_init()`、总线初始化、参数恢复和
处理/采集启动。原始采样到信号的映射及工程量换算留在业务层，硬件模块只返回样本。
参考结构与接口边界见[参考平台对照](../../docs/reference-platform.md)。

## 设备如何关联点表

`device.c` 中的 `static const DEV_TABLE_S devices[]` 同时填写设备号、总线端口、
采集地址及完整的 `ptl_tbl_cfg[]`，不用设备号分支在运行时补挂：
`proto_type` 标识表类型，`pst_table` 指向 const 表，`sig_num` 保存条目数。

| 设备 | `ptl_tbl_cfg[0]`：处理表 | `ptl_tbl_cfg[1]`：协议表 |
| --- | --- | --- |
| 本地设备 1 | `process.c` 的 `idu_local_process[]` | `PRO_MB_SLAVE` → `transmit.c` 的 `idu_transmit_table[]` |
| MV-IO 设备 10 | `process.c` 的 `idu_mvio_process[]` | `PRO_MB_MASTER` → `collect.c` 的 `idu_mvio_collect[]` |
| 可选本地业务设备 247 | `process.c` 的 `idu_imported_local_process[]` | 不单独挂协议表；寄存器映射由设备 1 的转发表引用 |
| 可选 DSP 设备 11–18 | 共用 `process.c` 的 `idu_imported_dsp_process[]` | `PRO_UPP_MASTER` → `collect.c` 的 `idu_imported_models` |

处理表类型统一为 `PRO_DATA_PROCESS`。`idu_register_point_tables()` 注册设备表 getter，
`idu_device_table()` 只返回 `devices`，`dp_init()` 取得完整关联并校验。
设备描述与点表都在 Flash，没有首次调用时修改配置的操作。
UPP 的 `sig_num` 使用 `IDU_IMPORTED_MODEL_FIELD_COUNT`，由工具统计所选模型字段数，
在 `collect.c` 用各模型字段数组的实际长度之和校验。模型内部数组保持 `static const`，
只导出设备绑定需要的注册表。

## 修改设备或测点

1. 用宏或枚举命名信号 ID，在 `process.c` 添加原格式行，需要处理时直接指定回调。
2. 在 `process.c` 的 `parameters[]` 同一行配置默认值和范围，通过
   `pltcb_reg_para_range_def_cb()` 提供给 DP。
   本地恒定标识使用 `idu_default_point` 初始化；采集点等待首次样本。
3. 在 `collect.c` 定义协议地址、长度、字序和周期编码。当前每个 Modbus 行独立请求，
   不跨未定义空洞合帧；`rqst_cyl` 经设备配置中的转换函数解释，不直接当毫秒。
4. 在 `transmit.c` 增加北向映射。新窗口也需加入
   `app/cfg/mvio.c` 的 `extensions[]`，控制命令在 `parameters.c` 声明。
   手写三表增删行后，同步修改 `inc/tables.h` 中相应数量；虚拟采集和大点表配置
   的条件分支也须匹配，数量不一致时构建报错。
5. 同型号 MV-IO 实例直接加入 `device.c` 的 `devices[]`，指定端口、从机地址，
   显式引用共享处理/采集表，并在 `transmit.c`
   显式补充该设备的北向映射；新总线仍须注册通信后端。
   检查 Kconfig 的测点、值池、采集任务和参数容量。持久点改变会影响 NVS schema。

质量寄存器映射、按样本年龄过期、旧参数导入及 NVS schema 自动迁移暂不接入应用。
待办与旧存储不匹配时的行为见[根 README](../../README.md#点表暂缓的扩展)。
通信请求超时、内部错误质量和基本参数保存/恢复继续有效。

## 国家参数预留接口

`process.c` 的 `parameters[]` 是一张 `static const` 数组，每行包含国家编号、
设备/信号 ID、`DM_SIG_DATA_S`（默认值、最小值、最大值）及主动恢复默认策略。
未指定 `country` 的条目使用通用配置 `IDU_PARAMETER_COUNTRY_COMMON = 0`。

通过 `inc/device.h` 中的只读接口查询：

```c
DM_SIG_DATA_S data;
bool restore;
int err = idu_point_parameters_get(IDU_PARAMETER_COUNTRY_COMMON,
                                   IDU_DEV_LOCAL, IDU_SIG_SAMPLE_PERIOD,
                                   &data, &restore);
```

- 当前只配置通用参数，没有预置任何具体国家的值。
- 增加国家时，在枚举中定义产品编号，并在同一数组的导入标记区外添加差异行。
  已配置国家优先匹配自身行，缺项复用通用行；每个国家/型号/信号键只能定义一次。
  国家行须填写完整的默认值、上下限和恢复策略，不逐字段合并。
- 没有通用记录但已定义的信号仍按原规则补零，范围全零表示使用类型范围。
  未定义的国家返回 `-ENOTSUP`；未定义的设备或信号返回 `-ENOENT`。
- 共用同一处理表的设备共享型号参数，参数键使用 `devices[]` 中该型号首个实例的设备号；
  当前 MV-IO 为 10、DSP 为 11。新增实例放在同型号首个实例之后，避免改变默认值来源。
  数组初值的返回指针只读。
- `restore` 表示允许主动恢复默认，不表示启动时强制覆盖已保存参数。
  查询不更新 DP 当前值、不读写 NVS。

当前 `idu_point_default()` / `idu_point_restore()` 均固定查询通用参数。
国家选择来源、保存方式和运行期切换暂未接入；后续接入时，在 DP 初始化前确定国家，
两个回调使用同一固定选择，并明确已有持久参数的处理规则。

## 更新导入点表

`tools/import_aidc.py` 更新 `.c` 和头文件中的成对标记区，保留标记外的手写内容：

- `process.c`：`points`、`defaults`；后者仅生成 `parameters[]` 内的通用参数行。
- `collect.c`：`fields`、`collect`；`transmit.c`：`transmit`。
- `device.c`：`devices`。
- `inc/tables.h`：`counts`，与导入数组一起更新数量枚举。

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
