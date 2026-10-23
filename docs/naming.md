# 按职责命名与采样配置

应用函数、类型、文件、日志和业务常量按职责命名。产品身份用于工程、板定义及应用 Kconfig；
通用模块使用自身的接口前缀，不携带产品前缀。讨论依据见
[已确认边界](../.scratch/responsibility-naming/discussion.md)。

## 应用接口

| 职责 | 当前接口或类型 |
| --- | --- |
| 整机启动 | `application_init()`，位于 `app/main.c` |
| 协议装配 | `protocol_init()`、`protocol_register_point_tables()` |
| 协议任务与端口 | `protocol_dp_tasks_*()`、`protocol_co_tasks_*()`、`protocol_ports_init()` |
| 每周期采样编排 | `acquisition_cycle()` |
| 产品采样 | `acquisition_inputs_init()`、`acquisition_inputs_sample()` |
| 样本与整机状态 | `struct acquisition_sample`、`struct application_status` |
| 测点业务 | `points_publish_snapshot()`、`points_parameter_command()`、`points_parameter_status()` |
| 工程换算 | `engineering_current_from_raw()` |
| 健康与镜像确认 | `status_init()`、`status_*()`、`image_confirm_checkpoint()` |

一次性装配与初始化接口使用 `*_init()`；同时提供默认配置和显式配置入口时，
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
`status/status.h`、`status/image_confirm/image_confirm.h`。
设备、信号、点表及逻辑口分别使用 `DEVICE_*`、`SIGNAL_*`、`TABLE_*`、`PORT_*` 等前缀。
日志来源使用 `app_main`、`app_protocol`、`app_acquisition`、`image_confirm`。

整机初始化期间处于 `STATUS_STARTING`；所有启动服务成功后进入 `STATUS_RUN`。
每轮采样、发布和健康检查完成后回到 `STATUS_RUN`；失败则保留故障阶段并锁定。

## 模块与产品配置

`input_sampler` 组合 `gpio` 与 `analog_input`，接收调用者的配置、数量和输出缓冲区。
模块不创建采样任务、不分配输出内存、不指定产品设备树节点或固定通道数。
采样错误使整个输出无效；缓冲区不足在开始读取前返回错误。
配置可只包含模拟组或数字组，模块构建仍依赖 ADC 与 GPIO。

应用 `acquisition/config.c` 从设备树组装配置并校验现有换算的电气条件。
八路 ADC、十六路 DI、四位拨码以及拨码加地址偏移的解释均属于应用。
工程换算和测点映射留在 `process`，协议与通信端口装配留在 `protocol`。

`dp_*`、`co_*`、`tr_*`、`iap_*`、`analog_input_*`、`digital_io_*`、`watchdog_*`
保留各自模块的命名空间。新的调用方直接使用职责接口。

## 兼容范围

工程目录 `IDU-pre`、板名 `idu_pre`、产品设备树标识及应用 `CONFIG_IDU_*` 保留产品身份。
原模块 `CONFIG_IDU_INPUTS` 改为通用 `CONFIG_INPUT_SAMPLER`；平均次数
`CONFIG_IDU_ADC_AVERAGE_COUNT` 在应用 Kconfig 中提供。

设备 ID、信号 ID、寄存器地址、Flash 分区及参数格式 magic 的数值保持不变。
去除 C 标识符前缀不修改线上协议或存储键，也不触发参数区迁移或擦除。
引脚、极性、通道顺序、平均次数、电气换算条件和运行周期保持原行为；实物验证与桌面回归分别记录。
