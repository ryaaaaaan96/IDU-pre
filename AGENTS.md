# IDU-pre 协作约定

## Git 工作流

- 每次完成一轮修改并执行相应验证后，自动在 `applications/IDU-pre/` 创建本地 Git 提交。
- West 清单改动在工作区的 `manifest/` 仓库单独提交；实际目录名为 `manifest/`。
- 提交范围限于这两个仓库，复用现有 Git 元数据，不在工作区根目录初始化仓库。推送需用户明确要求。
- 修改前检查 `git status --short`，保留用户已有的暂存和未暂存改动；只暂存、提交当前任务的文件或修改块。
- 提交说明简洁描述行为变化，完成后报告提交哈希和验证结果。无法完成验证或提交时如实说明原因。

## 代码与构建

- 产品代码、板级支持和点表模块保存在本应用内；依赖版本由工作区 `manifest/west.yml` 管理。
- `modules/gd32e505` 通过 Zephyr `soc_root` / `dts_root` 提供 E505 SoC、设备树时钟配置和 ADC0 驱动；本板保持 20 MHz bypass / 180 MHz；`modules/hardware.cmake` 供应用和 Sysbuild 共用。ADC 寄存器与 HAL 调用归模块驱动，`modules/analog_input` 通过标准 ADC API 处理通道组及平均，`modules/gpio/src/idu_inputs.c` 从设备树装配产品配置；芯片代码不包含 IDU 专用 GPIO 策略；JTAG/SWD 配置和 MCUboot 的 PC13 浮空处理归 `boards/idu/idu_pre/board.c` 的早期钩子。`sysbuild/` 保留标准薄入口，产品镜像配置与分区归 `bootloader/`。
- 通用点表模块使用 `modules/dataprocess`、`modules/collect`、`modules/transmit`，接口分别为 `dp_*`、`co_*`、`tr_*`；独立块解码算法归 `modules/quicklz`，接口为 `qlz_decode_level3()`；升级接收、SGZ 和验签归 `modules/iap`，接口为 `iap_*`；产品接口继续使用 `idu_*`。
- `modules/vport` 提供协议无关的逻辑端口，接口为 `vport_*`。物理后端通过配置注册，原始 UART/CAN/socket 适配器后续放在 `vport/backends/` 按需编译；核心不依赖驱动、DP 或 Modbus。链路、端口及路由表在启动期注册且保持 const；每条链路统一发送锁，接收使用有界共享池和单工作线程。`modules/transmit/src/modbus_transport.c` 已将现有 RS485 转发、虚拟采集及可配置的独立 RTU 采集口在完整 ADU 边界接入，绑定归 `app/cfg/ports.c`；保留 Zephyr 的组帧、CRC、串口配置和 DE 控制。当前板上 RS485 只做转发，不复用双主角色。流组帧须保留 link/peer 来源及丢失序号；不能把 UART/GPIO emulator 或 CAN 核心测试表述为实物验证。
- 业务接口尽量沿用参考平台的 `data_process.h`、`collect_lib.h`、`transmit_lib.h`、`platform_callback.h` 及非 ARC 命名，直接使用标准 C/Zephyr 类型，不封装 ARC 层。同步字节接口 `dp_set_sig_value()` 与异步字接口 `dp_submit_sig_value(s)` 不混用；原生配置直接初始化用 `dp_init_config()`，注册产品配置后用 `dp_init()`。参考库的反汇编只作为行为证据，不链接旧库；支持范围及明确差异见 `docs/reference-platform.md`。
- `app/main.c` 集中模块启动顺序、采样周期和休眠，不单设 startup 文件；`acquisition` 只编排每周期采样、发布、转发和健康检查。`app/status/` 管理业务阶段、故障、LED、喂狗进度及试运行确认条件。`app/process/points.c` 只负责本地样本映射和公共业务回调，工程换算归 `engineering.c`；不保留 `idu_equipment_*` 兼容入口。
- 通用硬件服务归 `modules/analog_input`、`modules/gpio`、`modules/watchdog`。`idu_inputs_sample()` 只返回原始值；引脚、极性、位序、时序归设备树，平均次数归 Kconfig。LED 直接在 `status.c` 使用 Zephyr LED API，启动/故障常亮、健康采样翻转；有复用需求时再抽模块。外部看门狗使用 `WATCHDOG_MONITOR`；GPIO 的 `DIGITAL_IO` 和 `IDU_INPUTS` 可分别启用。
- 产品升级开关归 `app/Kconfig`，`modules/iap` 从设备树选择分区和内存，从 Kconfig 读取密钥来源、镜像上限和保存策略。调用方直接使用 `<iap/iap.h>`；不设 `app/iap/` 兼容目录。健康确认策略归 `app/status/`。
- 源码由 `app/CMakeLists.txt` 及各功能目录的 CMakeLists 集中管理；头文件与所属功能源码同目录，跨功能引用以 `app/` 为搜索根目录，通用 `dp_*`、`co_*`、`tr_*` 实现留在 `modules/`。
- `app/cfg/` 保存点表及各模块的产品配置；不实现端口状态机、协议解析或收发，`device.c` 用 const `DEV_TABLE_S` 直接声明设备与点表关联并注册回调，getter 只返回配置，`inc/` 集中所有配置头文件，`tables/` 保存采集/处理/转发三表及默认值。三表使用参考结构，默认/范围及恢复策略集中在 `process.c` 的 const `parameters[]`，预留按国家查询接口，当前 DP 固定使用通用参数；信号用枚举或宏，直接绑定回调，不再设置 `point_metadata`。质量寄存器映射、采样过期及 NVS 迁移暂缓，见根 README。三表直接在 `.c` 中维护，不使用 `.inc` 片段；三表由 CMake 独立编译，`inc/tables.h` 通过不带长度的 `extern const` 数组声明及数量枚举提供静态设备绑定，定义处用 `ARRAY_SIZE` / `BUILD_ASSERT` 校验实际行数；手写点表增删行同步修改枚举，导入数量由工具更新 `counts` 标记区；导入工具只更新标记区，符号写入 `inc/imported_signals.h`，来源记录保存在 `docs/point-table-import.json`。不再设置 `app/equipment/` 或通用 equipment 模块；产品装配归 `app/`，队列、数据池、处理调度、采集和转发机制归各通用模块。协议选择、控制命令和完整 ADU 扩展配置归 `app/cfg/`，产品选项归 `app/Kconfig`。`app/process/` 执行测点处理和参数命令，协议编码及 Zephyr 端口适配归 `modules/transmit`，采集和可选模拟设备归 `modules/collect`；扩展步骤见 `app/cfg/README.md`。`conf/` 保存应用可选配置，MCUboot 配置、分区和联合构建设置归 `bootloader/`，`sysbuild/` 只保留 Zephyr 薄入口。目录整理保持既有配置内容；重排持久化点前，必须核对 NVS 和线协议兼容性。
- 在应用目录执行 `./build.sh` 构建 MCUboot + IAPv3；`--smp-uart` 选择 SMP，`--no-upload` 关闭上传入口。旧 BOOT 构建不再提供。
- 当前工作固件启用 MCUboot + IAPv3。采集总线数量及栈大小由 `modules/collect/Kconfig` 定义，隔离转发口数量、转发栈及共享重配置栈由 `modules/transmit/Kconfig` 定义；数值保留在 `prj.conf` 手动维护，不按固件类型自动覆盖，也不预留未来端口。扩展时手动修改 `.conf`，多端口测试在自己的配置中显式声明容量。
- 在应用目录执行 `bash tests/run.sh` 运行完整回归；按改动范围选择必要验证。文档修改检查内容、路径和格式即可。
- 构建与测试产物放在工作区 `build/`，签名私钥放在 `build/keys/`，均不提交。
- 分开记录主机测试、模拟器验证与实物验证，不能以编译通过代替实物验证。

- 应用不再设置 `communication/`。`cfg/ports.c` 声明传输绑定，`forwarding.c` 选择转发协议，`parameters.c` 声明状态/命令寄存器并通过 `extension` 直接绑定 `iap_route()`，`simulation.c` 声明可选仿真数据。`tr_modbus_network_start()` 注册网络，`co_port_open()` 打开采集端口，`tr_server_service()` 管理各转发口；`acquisition` 直接调用模块接口。AIDC 参数解释保留在 `process/serial_settings.c`。IAP 与仿真能力通过独立 Kconfig 开关启用；模块不得包含产品头文件或直接读取 IDU 设备树别名。

- 不恢复旧 MV-IO 特殊协议、247 地址门控、跨空洞跳读、补零保留区、无效写 ACK 或旧 BOOT 路径迁移。当前 dp_/co_/tr_ 参考接口和实际采集设备点表继续保留；IAPv3 的线协议仍由 iap 模块维护。产品分区集中在 `bootloader/partitions.dtsi`，板级 DTS 引用，IAP 暂存/日志由 overlay 补充。
