# 按职责命名与采样配置

应用函数、类型、文件、日志和业务常量按职责命名。产品身份用于工程、板定义及应用 Kconfig；
通用模块使用自身的接口前缀，不携带产品前缀。讨论依据见
[已确认边界](../.scratch/responsibility-naming/discussion.md)。

## 应用接口

| 职责 | 当前接口或类型 |
| --- | --- |
| 整机启动 | `startup()`，位于 `app/main.c` |
| 协议装配 | `protocol_init()`、`protocol_register_point_tables()` |
| 协议任务与端口 | `protocol_dp_tasks_*()`、`protocol_co_tasks_*()`、`protocol_ports_init()` |
| 每周期采样编排 | `acquisition_cycle()` |
| 产品采样 | `acquisition_inputs_init()`、`acquisition_cycle()` |
| 输入映射 | `struct analog_point` 及 DI 位图配置，含 spec、device_id、signal_id |
| 测点业务 | `points_parameter_command()`、`points_parameter_status()` |
| 工程换算 | `acquisition.c` 的 AI 采样循环内直接换算 |
| 健康与镜像确认 | `status_init()`、`status_*()`、`image_confirm_checkpoint()` |

整机启动编排入口使用 `startup()`，集中组织各服务初始化。
模块的一次性装配与初始化接口使用 `*_init()`；同时提供默认配置和显式配置入口时，
分别使用 `*_init()` 与 `*_init_config(config)`，例如 IAP 的两种初始化方式。
运行期启动操作保留 `*_start()`，例如放行已创建的线程和启动测点定时器。

| 初始化内容 | 接口 |
| --- | --- |
| IAP 项目配置 / 显式配置 | `iap_init()` / `iap_init_config(config)` |
| 看门狗实例 | `watchdog_init()` |
| DP 测点处理计划 / 全局 INIT 与 TIMER 表 | `dp_process_init()` / `dp_reference_init()` |
| 采集调度就绪 / 测试模拟器 | `co_polling_init()` / `co_simulator_init()` |
| 应用任务上下文 / 转发服务 | `protocol_dp_tasks_init()`、`protocol_co_tasks_init()` / `protocol_forwarding_init()` |

`protocol_dp_tasks_start()`、`protocol_co_tasks_start()` 最后放行已初始化的任务；
`dp_process_timer_start()` 是运行期的定时器操作。这些接口保留启动含义，初始化顺序与执行上下文归属保持原约定。

头文件与职责一致，例如 `acquisition/acquisition.h`、`process/points.h`、
`status/status.h`、`status/status_led/status_led.h`、`status/image_confirm/image_confirm.h`。
设备、信号、点表及逻辑口分别使用 `DEVICE_*`、`SIGNAL_*`、`TABLE_*`、`PORT_*` 等前缀。
日志来源使用 `app_main`、`app_protocol`、`app_acquisition`、`image_confirm`。

整机初始化期间处于 `STATUS_STARTING`；所有启动服务成功后进入 `STATUS_RUN`。
运行期间保持 `STATUS_RUN`；不设置采样、发布和健康检查的细分阶段，终止故障仍锁定。

## 模块与产品配置

应用直接调用 `adc_channel_setup_dt()` 配置 ADC 通道，通过 `adc_sequence_init_dt()`
与 `adc_read_dt()` 读取样本并按产品要求做软件平均，不单设模拟量包装模块。
数字输入由应用直接使用 Zephyr GPIO API 配置和读取，不保留 GPIO 包装模块。
DAC 尚未使用，后续有硬件需求时优先使用 Zephyr 标准接口。

应用 `acquisition/acquisition.c` 从设备树组装配置并校验现有换算的电气条件。
八路 ADC 和十六路 DI 的目标设备/信号均由应用映射配置。拨码地址暂不采样。
电流换算和输入映射归 `acquisition`，公共业务回调归 `process`，协议与通信端口装配归 `protocol`。

`dp_*`、`co_*`、`tr_*`、`iap_*`、`watchdog_*`
保留各自模块的命名空间。新的调用方直接使用职责接口。

## 兼容范围

工程目录 `IDU-pre`、板名 `idu_pre`、产品设备树标识及应用 `CONFIG_IDU_*` 保留产品身份。
ADC 由 `CONFIG_ADC` 启用；平均次数
`CONFIG_IDU_ADC_AVERAGE_COUNT` 在应用 Kconfig 中提供。

设备 ID、信号 ID、寄存器地址、Flash 分区及参数格式 magic 的数值保持不变。
去除 C 标识符前缀不修改线上协议或存储键，也不触发参数区迁移或擦除。
引脚、极性、通道顺序、平均次数、电气换算条件和运行周期保持原行为；实物验证与桌面回归分别记录。

## 输入名称映射

ADC 通过 `io-channel-names` 提供 current-1 至 current-8，应用使用
`ADC_DT_SPEC_GET_BY_NAME` 选择，设备树条目可以任意排列，硬件通道号不要求连续。
DI 使用 di1-gpios 至 di16-gpios，按名称引用 GPIO 后合成为一个位图测点。
ADC 每行配置 spec、device_id 和 signal_id；DI 位图整体配置目标 device_id/signal_id。
采样后直接同步写 DP，无本地快照消息、堆批次或异步发布。AI 保留平均和电流换算。
地址测点保留点表默认值，硬件拨码不参与初始化和采样。
后续替换业务名称时同步修改设备树、绑定和应用引用，不用数字列表索引取配置。
