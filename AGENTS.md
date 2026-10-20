# IDU-pre 协作约定

## Git 工作流

- 每次完成一轮修改并执行相应验证后，自动在 `applications/IDU-pre/` 创建本地 Git 提交。
- West 清单改动在工作区的 `manifest/` 仓库单独提交；实际目录名为 `manifest/`。
- 提交范围限于这两个仓库，复用现有 Git 元数据，不在工作区根目录初始化仓库。推送需用户明确要求。
- 修改前检查 `git status --short`，保留用户已有的暂存和未暂存改动；只暂存、提交当前任务的文件或修改块。
- 提交说明简洁描述行为变化，完成后报告提交哈希和验证结果。无法完成验证或提交时如实说明原因。

## 代码与构建

- 产品代码、板级支持和点表模块保存在本应用内；依赖版本由工作区 `manifest/west.yml` 管理。
- 通用点表模块使用 `modules/dataprocess`、`modules/collect`、`modules/transmit`，接口分别为 `dp_*`、`co_*`、`tr_*`；独立块解码算法归 `modules/quicklz`，接口为 `qlz_decode_level3()`；产品接口继续使用 `idu_*`。
- 业务接口尽量沿用参考平台的 `data_process.h`、`collect_lib.h`、`transmit_lib.h`、`platform_callback.h` 及非 ARC 命名，直接使用标准 C/Zephyr 类型，不封装 ARC 层。同步字节接口 `dp_set_sig_value()` 与异步字接口 `dp_submit_sig_value(s)` 不混用；原生配置直接初始化用 `dp_init_config()`，注册产品配置后用 `dp_init()`。参考库的反汇编只作为行为证据，不链接旧库；支持范围及明确差异见 `docs/reference-platform.md`。
- `src/main.c` 负责采样周期和休眠，`src/app/runtime.c` 编排启动、采集和故障策略；`src/app/startup.c` 显式装配 DP、采集总线、参数恢复与处理启动，`src/app/point_service.c` 将本地样本映射到 DP 信号；硬件访问归 `src/hardware/`，工程量换算归 `src/process/engineering.c`，端口和协议适配归 `src/communication/`，产品升级流程与校验规则归 `src/iap/`。
- 源码由 `src/CMakeLists.txt` 及各功能目录的 CMakeLists 集中管理；公开头文件继续放在 `include/`，通用 `dp_*`、`co_*`、`tr_*` 实现留在 `modules/`。
- `src/cfg/` 仅保存点表相关内容，分为两组：`devices/` 管设备实例、型号及三表装配，`tables/` 管信号、采集/处理/转发三表及点表元数据。三表直接在 `.c` 中维护，不使用 `.inc` 片段；导入工具只更新标记区，符号写入 `tables/imported_signals.h`，来源记录保存在 `docs/point-table-import.json`。不再设置 `src/equipment/` 或通用 equipment 模块；产品装配归 `src/app/`，队列、数据池、处理调度、采集和转发机制归各通用模块。协议选择、控制命令和旧协议路由配置归 `src/communication/config/`，产品选项归 `src/Kconfig`。`src/process/` 执行测点处理和参数命令，协议编码及 Zephyr 端口适配归 `src/communication/`；扩展步骤见 `src/cfg/README.md`。`conf/` 保存可选 Kconfig 配置组合。目录整理保持既有配置内容；重排持久化点或旧协议回调顺序前，必须核对 NVS 和线协议兼容性。
- 在应用目录执行 `./build.sh` 构建默认固件，或 `./build.sh --mcuboot --iapv3` 构建兼容升级配置。
- 在应用目录执行 `bash tests/run.sh` 运行完整回归；按改动范围选择必要验证。文档修改检查内容、路径和格式即可。
- 构建与测试产物放在工作区 `build/`，签名私钥放在 `build/keys/`，均不提交。
- 分开记录主机测试、模拟器验证与实物验证，不能以编译通过代替实物验证。
