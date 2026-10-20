# IDU-pre 协作约定

## Git 工作流

- 每次完成一轮修改并执行相应验证后，自动在 `applications/IDU-pre/` 创建本地 Git 提交。
- West 清单改动在工作区的 `manifest/` 仓库单独提交；实际目录名为 `manifest/`。
- 提交范围限于这两个仓库，复用现有 Git 元数据，不在工作区根目录初始化仓库。推送需用户明确要求。
- 修改前检查 `git status --short`，保留用户已有的暂存和未暂存改动；只暂存、提交当前任务的文件或修改块。
- 提交说明简洁描述行为变化，完成后报告提交哈希和验证结果。无法完成验证或提交时如实说明原因。

## 代码与构建

- 产品代码、板级支持和点表模块保存在本应用内；依赖版本由工作区 `manifest/west.yml` 管理。
- `modules/gd32e505` 通过 Zephyr `soc_root` 提供 E505 SoC 和当前 20 MHz bypass / 180 MHz 时钟接入；`modules/hardware.cmake` 供应用和 Sysbuild 共用。芯片代码不包含 IDU 专用 GPIO 策略；PC13 MCUboot 看门狗处理归 `boards/idu/idu_pre/board.c` 的早期钩子。`sysbuild/` 保留标准薄入口，产品镜像配置与分区归 `bootloader/`。
- 通用点表模块使用 `modules/dataprocess`、`modules/collect`、`modules/transmit`，接口分别为 `dp_*`、`co_*`、`tr_*`；独立块解码算法归 `modules/quicklz`，接口为 `qlz_decode_level3()`；产品接口继续使用 `idu_*`。
- 业务接口尽量沿用参考平台的 `data_process.h`、`collect_lib.h`、`transmit_lib.h`、`platform_callback.h` 及非 ARC 命名，直接使用标准 C/Zephyr 类型，不封装 ARC 层。同步字节接口 `dp_set_sig_value()` 与异步字接口 `dp_submit_sig_value(s)` 不混用；原生配置直接初始化用 `dp_init_config()`，注册产品配置后用 `dp_init()`。参考库的反汇编只作为行为证据，不链接旧库；支持范围及明确差异见 `docs/reference-platform.md`。
- `app/main.c` 负责采样周期和休眠，`app/runtime.c` 编排启动、采集和故障策略；`app/startup.c` 显式装配 DP、采集总线、参数恢复与处理启动，`app/point_service.c` 将本地样本映射到 DP 信号；硬件访问归 `app/hardware/`，工程量换算归 `app/process/engineering.c`，端口和协议适配归 `app/communication/`，产品升级流程与校验规则归 `app/iap/`。
- 源码由 `app/CMakeLists.txt` 及各功能目录的 CMakeLists 集中管理；头文件与所属功能源码同目录，跨功能引用以 `app/` 为搜索根目录，通用 `dp_*`、`co_*`、`tr_*` 实现留在 `modules/`。
- `app/cfg/` 仅保存点表相关内容，分为两组：`devices/` 管设备实例、型号及三表装配，`tables/` 管信号、采集/处理/转发三表及点表元数据。三表直接在 `.c` 中维护，不使用 `.inc` 片段；导入工具只更新标记区，符号写入 `tables/imported_signals.h`，来源记录保存在 `docs/point-table-import.json`。不再设置 `app/equipment/` 或通用 equipment 模块；产品装配归 `app/`，队列、数据池、处理调度、采集和转发机制归各通用模块。协议选择、控制命令和旧协议路由配置归 `app/communication/config/`，产品选项归 `app/Kconfig`。`app/process/` 执行测点处理和参数命令，协议编码及 Zephyr 端口适配归 `app/communication/`；扩展步骤见 `app/cfg/README.md`。`conf/` 保存应用可选配置，MCUboot 配置、分区和联合构建设置归 `bootloader/`，`sysbuild/` 只保留 Zephyr 薄入口。目录整理保持既有配置内容；重排持久化点或旧协议回调顺序前，必须核对 NVS 和线协议兼容性。
- 在应用目录执行 `./build.sh` 构建默认固件，或 `./build.sh --mcuboot --iapv3` 构建兼容升级配置。
- 在应用目录执行 `bash tests/run.sh` 运行完整回归；按改动范围选择必要验证。文档修改检查内容、路径和格式即可。
- 构建与测试产物放在工作区 `build/`，签名私钥放在 `build/keys/`，均不提交。
- 分开记录主机测试、模拟器验证与实物验证，不能以编译通过代替实物验证。
