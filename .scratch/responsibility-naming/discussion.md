# 按职责命名与硬件配置边界讨论

状态：Q1～Q7 已全部确认；代码已按本约定迁移，验证结果见同目录 validation.md。

## 已确认

- 应用函数、类型、头文件及日志来源按职责命名，减少产品前缀。例如 protocol_init、acquisition_cycle、status_start、image_confirm_checkpoint；状态类型使用 application_status。
- 保留通用模块已有的 dp_、co_、tr_、iap_ 等接口命名空间。
- modules 中去掉 IDU 产品前缀；具体硬件绑定移到应用配置，通过配置交给模块。
- 保留可配置的统一采样入口。模块接收应用提供的 ADC/GPIO 配置、数量及输出缓冲区；地址解释、工程换算及测点映射由应用负责。
- 应用采样配置位于 app/acquisition/config.c，从设备树构造配置后交给模块；协议配置继续位于 app/protocol。
- 工程目录、板名、应用侧 CONFIG_IDU_* 保留产品身份；模块侧配置改为功能命名。
- 设备 ID、信号 ID、寄存器地址及持久化格式标识保持现有数值，避免命名调整影响通信和掉电参数兼容。
- 新增独立 modules/input_sampler，组合 gpio 和 analog_input 的采样能力；gpio 保持数字输入职责。
- 应用设备、信号、点表宏与枚举采用 DEVICE_*、SIGNAL_*、TABLE_* 等职责前缀，保留原值。例如 IDU_DEV_LOCAL → DEVICE_LOCAL、IDU_DEV_REMOTE_IO → DEVICE_REMOTE_IO。
- 最终落地时同步本仓库 AGENTS.md 中已有的 idu_* 命名规则及相关引用。

## 核查事实

- modules/gpio/src/idu_inputs.c 直接读取 idu_io 设备树节点，固定八路 ADC、十六路 DI、四位拨码，并检查 3.3V/12bit 电气条件。
- analog_input_config 和 digital_io_config 已支持由调用者提供通道或引脚数组及数量，可用于配置注入。
- 设备与信号的数值参与持久化身份；参数区和意图日志的 magic 含旧产品字节。命名调整与这些数值是否变化是不同决策。

## 目标职责分配

| 位置 | 职责 |
| --- | --- |
| app/acquisition/config.c | 从产品设备树装配 ADC/GPIO 配置、通道数、采样参数和输出缓冲区。 |
| app/acquisition | 调用统一采样接口，解释产品地址及编排每周期工作。 |
| app/process | 工程换算、测点映射及业务回调。 |
| app/protocol | 点表、协议、通信端口和任务编排。 |
| app/status | 整机状态、LED、看门狗及镜像健康确认。 |
| modules/input_sampler | 由调用者配置驱动的统一采样，不固定产品通道布局，不解释设备站号，不映射测点。 |
| modules/gpio / modules/analog_input | 提供各自的底层采样能力。 |

产品现有八路 ADC、十六路 DI、四位拨码及电气换算条件由应用配置和校验表达；迁移保持实际引脚、采样顺序、电气约束和换算结果。
输出使用应用提供的缓冲区；接口具体声明及错误检查在实施时结合现有 API 落实。

## 迁移与验收

1. 同步应用函数、类型、文件、头文件 guard、日志来源及业务标识的命名；更新调用方、构建、测试和当前使用文档。
2. 将原产品采样组合迁为 input_sampler 通用实现，并在应用配置中装配实际硬件；同步 Kconfig 与模块元数据。
3. 模块源码不读取产品专属设备树节点，不固定产品通道数、地址偏移或电气换算条件；模块 IDU 产品前缀全部消除。
4. 设备/信号 ID、寄存器地址、协议行为及 NVS 格式标识保持原值；名称调整不触发存储格式迁移。
5. 同步应用 AGENTS.md 的命名与采样职责约定；历史验证记录保留其当时语境。
6. 执行采样模块、采集与状态、测点及参数回归，并构建当前 MCUboot + IAPv3 固件。可配置通道数量、容量不足及采样失败应有必要验证；实物引脚、时序、升级验证单独记录。
7. 保留用户已有未提交修改；完成实现和验证后只提交本轮任务改动。

讨论已无未决边界。设计记录提交 a88851b 仅记录约定；后续迁移与验证由 [实施记录](validation.md) 单独说明。
