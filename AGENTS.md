# IDU-pre 协作约定

## Git 工作流

- 每次完成一轮修改并执行相应验证后，自动在 `applications/IDU-pre/` 创建本地 Git 提交。
- 仅允许修改 `applications/` 下的应用仓库。工作区根目录下的 `manifest/`、`zephyr/`、`modules/`、`bootloader/` 等仓库保持只读。
- 当前项目仅在 `applications/IDU-pre/` 创建本地提交，复用现有 Git 仓库，不在工作区根目录初始化仓库。推送需用户明确要求。
- 修改前检查 `git status --short`，保留用户已有的暂存和未暂存改动；只暂存、提交当前任务的文件或修改块。
- 提交说明简洁描述行为变化，完成后报告提交哈希和验证结果。无法完成验证或提交时如实说明原因。

## 代码与构建

- 产品代码、板级支持和点表模块保存在本应用内；依赖版本由工作区 `manifest/west.yml` 管理。
- `modules/gd32e505` 通过 Zephyr `soc_root` / `dts_root` 提供 E505 SoC、设备树时钟配置和 ADC0 驱动；本板保持 20 MHz bypass / 180 MHz；`modules/hardware.cmake` 供应用和 Sysbuild 共用。ADC 寄存器与 HAL 调用归模块驱动，`modules/analog_input` 通过标准 ADC API 处理通道组及平均，`modules/gpio/src/idu_inputs.c` 从设备树装配产品配置；芯片代码不包含 IDU 专用 GPIO 策略；JTAG/SWD 配置和 MCUboot 的 PC13 浮空处理归 `boards/idu/idu_pre/board.c` 的早期钩子。`sysbuild/` 保留标准薄入口，产品镜像配置与分区归 `bootloader/`。
- 通用点表模块使用 `modules/dataprocess`、`modules/collect`、`modules/transmit`，接口分别为 `dp_*`、`co_*`、`tr_*`；独立块解码算法归 `modules/quicklz`，接口为 `qlz_decode_level3()`；升级接收、SGZ 和验签归 `modules/iap`，接口为 `iap_*`；产品接口继续使用 `idu_*`。
- `modules/vport` 只提供协议无关的完整报文端口登记、回调调用、载荷检查及逻辑口发送互斥，不识别硬件、不维护路由表、不设置 backends 或 RX 线程。应用通过 CO/TR 的 `message.h` 注册和输入，模块的 `src/message.c` 内部调用 vport；配置常驻，运行期不替换。实际端口映射和 source.link/peer 由 `app/protocol/ports.c` 编排；物理资源锁及事务仲裁归接入实现。当前按用户指定将采集和转发共用一条 RS485 做性能联调；cfg 对完整南向事务加锁并分流应答，不能宣称具备外部多主仲裁。北向普通读继续读取 DP 缓存。
- `collect` 和 `transmit` 各自的 `protocols/modbus/raw.c` 负责 RAW ADU 与完整 RTU 字节帧转换，`CO_MODBUS_RAW_PORT` / `TR_MODBUS_RAW_PORT` 分别启用并依赖各自 `CO_VPORT` / `TR_VPORT`。协议运行 RAW，实际 UART 继续由 Zephyr 负责组帧、CRC 和 DE；共享构建期兼容位于 `transmit/protocols/modbus/compat`，collect 也加载它但不启用 transmit 服务。原 ADU 交接环路已删除，不新增 modbus_transport 模块。RAW 接收忙保护、发送错误传递、静默处理完成通知必须保留。可选 CO_SIMULATOR 模拟服务端，仍依赖转发适配。详见 docs/communication-ports.md。
- 业务接口尽量沿用参考平台的 `data_process.h`、`collect_lib.h`、`transmit_lib.h`、`platform_callback.h` 及非 ARC 命名，直接使用标准 C/Zephyr 类型，不封装 ARC 层。同步字节接口 `dp_set_sig_value()` 与异步字接口 `dp_submit_sig_value(s)` 不混用；原生配置直接初始化用 `dp_init_config()`，注册产品配置后用 `dp_init()`。参考库的反汇编只作为行为证据，不链接旧库；支持范围及明确差异见 `docs/reference-platform.md`。
- `app/main.c` 集中模块启动顺序、采样周期和休眠，不单设 startup 文件；`acquisition` 只编排每周期采样、发布、转发和健康检查。`app/status/` 管理业务阶段、故障、LED、喂狗进度及试运行确认条件。`app/process/points.c` 只负责本地样本映射和公共业务回调，工程换算归 `engineering.c`；不保留 `idu_equipment_*` 兼容入口。
- 通用硬件服务归 `modules/analog_input`、`modules/gpio`、`modules/watchdog`。`idu_inputs_sample()` 只返回原始值；引脚、极性、位序、时序归设备树，平均次数归 Kconfig。LED 直接在 `status.c` 使用 Zephyr LED API，启动/故障常亮、健康采样翻转；有复用需求时再抽模块。外部看门狗使用 `WATCHDOG_MONITOR`；GPIO 的 `DIGITAL_IO` 和 `IDU_INPUTS` 可分别启用。
- 产品升级开关归 `app/Kconfig`，`modules/iap` 从设备树选择分区和内存，从 Kconfig 读取密钥来源、镜像上限和保存策略。调用方直接使用 `<iap/iap.h>`；不设 `app/iap/` 兼容目录。健康确认策略归 `app/status/`。
- 源码由 `app/CMakeLists.txt` 及各功能目录的 CMakeLists 集中管理；头文件与所属功能源码同目录，跨功能引用以 `app/` 为搜索根目录，通用 `dp_*`、`co_*`、`tr_*` 实现留在 `modules/`。
- `app/protocol/` 保存产品协议配置。`protocol.c` 声明设备与表标识关联、周期转换、CO/TR 服务和 IAP 绑定；`ports.c` 编排物理端口；`inc/` 仅放对外头文件，内部 `tables.h`、`ports.h` 放目录根部。三表独立编译，显式填写结构行；process 行内填写 data 默认值/范围，sig_flag 使用 FLAG_RANGE_CHECK 和 FLAG_NO_RESTORE 控制检查及恢复，由 DP 直接读取；国家选择固定在 DP 初始化前，产品使用通用配置；每张 static const 数组使用 Zephyr Iterable Sections 登记一次，以 ARRAY_SIZE 自动计数。通用校验/装配归 DP 的 DP_TABLE_REGISTRY；点表及登记项在 Flash，仅 DEV_TABLE_S 关联视图占少量 RAM，启动后不可修改。不得恢复 extern 数组、手工数量枚举、包含 .c、逐行宏或 point_metadata。应用只维护 IDU 产品点表与业务，不保留 AIDC 产品分支、导入工具或专用测试。IAPv3 是独立升级模块，不是产品方案；其开关不切换业务点表。
- FLAG_COLLECT 表示南向下发，与 FLAG_PERSISTENCE 独立。非持久命令只写一次，不读回、不写 NVS、不自动重放；持久设置保留意图日志和读回恢复。下发点非零周期可以同时轮询，反馈只更新缓存，不能覆盖已保存目标。现用 IAPv3、参数状态/命令寄存器和恢复入口保留。状态使用 FLAG_READ_CALLBACK 在读取时产生快照；命令通过 dp_defer_para_action 登记，整笔校验通过后同步执行，不存储触发值，不在初始化或读取时重放。南向周期采集 DI 和两路模拟量，周期由 .conf 手动设置；协议号和地址只初始化读取。本地模拟量保持现有换算单位，变更须明确协议含义。
- 在应用目录执行 `./build.sh` 构建 MCUboot + IAPv3；`--smp-uart` 选择 SMP，`--no-upload` 关闭上传入口。旧 BOOT 构建不再提供。
- 当前工作固件启用 MCUboot + IAPv3。采集总线数量及栈大小由 `modules/collect/Kconfig` 定义，隔离转发口数量、转发栈及共享重配置栈由 `modules/transmit/Kconfig` 定义；数值保留在 `prj.conf` 手动维护，不按固件类型自动覆盖，也不预留未来端口。扩展时手动修改 `.conf`，多端口测试在自己的配置中显式声明容量。
- 在应用目录执行 `bash tests/run.sh` 运行完整回归；按改动范围选择必要验证。文档修改检查内容、路径和格式即可。
- 构建与测试产物放在工作区 `build/`，签名私钥放在 `build/keys/`，均不提交。
- 分开记录主机测试、模拟器验证与实物验证，不能以编译通过代替实物验证。

- 应用不再设置 `communication/`。`protocol/ports.c` 声明传输绑定并实现应用收发编排回调，`protocol.c` 选择转发设备和广播策略，`tables/segments.c` 以 `MB_TABLE_SEG_T` 经 `PRO_ADDR_SLAVE` 挂载 IAP；协议段按首地址分派并自行校验变长报文，标准 `tr_segment` 保留完整范围检查；状态/命令在 process 表绑定回调，transmit 表只映射地址，模拟配置已移至 `tests/fixtures/`，产品不编译模拟从机。`main.c` 调用 `idu_ports_init()` 注册 CO/TR RAW 端口、应用回调和实际串口接入，`co_port_open()` 打开采集端口、co_init_bus 注册 idu_collect_bus，`tr_server_service()` 管理各转发口；`acquisition` 直接调用模块接口。IAP 由独立 Kconfig 开关启用，通用模拟后端仅用于模块测试；模块不得包含产品头文件或直接读取 IDU 设备树别名。

- 不恢复旧 MV-IO 特殊协议、247 地址门控、跨空洞跳读、补零保留区、无效写 ACK 或旧 BOOT 路径迁移。当前 dp_/co_/tr_ 参考接口和实际采集设备点表继续保留；IAPv3 的线协议仍由 iap 模块维护。产品分区集中在 `bootloader/partitions.dtsi`，板级 DTS 引用，IAP 暂存/日志由 overlay 补充。

- 产品点表、设备绑定、启动计划、服务和端口配置逐字段显式初始化，包括点表 data 的三个值。缺省值使用所属模块的编译期字段常量，见 modules/README.md；标识、地址、类型、长度和周期由应用明确配置。Zephyr 联合体只初始化所选模式的成员，运行态对象按模块接口初始化，不使用自定义整行宏代替配置。

## Agent skills

### Issue tracker

使用本地 Markdown 记录任务和规格，通过本仓库的 Git 提交保存。
具体约定见 [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md)。

### Domain docs

采用单一领域布局：仓库根目录 `GLOSSARY.md` 与 `docs/adr/`。
读取规则见 [docs/agents/domain.md](docs/agents/domain.md)。
