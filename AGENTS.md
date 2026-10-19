# IDU-pre 协作约定

## Git 工作流

- 每次完成一轮修改并执行相应验证后，自动在 `applications/IDU-pre/` 创建本地 Git 提交。
- West 清单改动在工作区的 `manifest/` 仓库单独提交；实际目录名为 `manifest/`。
- 提交范围限于这两个仓库，复用现有 Git 元数据，不在工作区根目录初始化仓库。推送需用户明确要求。
- 修改前检查 `git status --short`，保留用户已有的暂存和未暂存改动；只暂存、提交当前任务的文件或修改块。
- 提交说明简洁描述行为变化，完成后报告提交哈希和验证结果。无法完成验证或提交时如实说明原因。

## 代码与构建

- 产品代码、板级支持和点表模块保存在本应用内；依赖版本由工作区 `manifest/west.yml` 管理。
- 通用模块使用 `modules/dataprocess`、`modules/collect`、`modules/transmit`，接口分别为 `dp_*`、`co_*`、`tr_*`；产品接口继续使用 `idu_*`。
- `src/main.c` 负责采样周期和休眠，`src/app/runtime.c` 编排启动、采集和故障策略；硬件访问归 `src/hardware/`，设备服务与工程量换算归 `src/equipment/`，端口和协议适配归 `src/communication/`，升级实现归 `src/iap/`。
- 源码由 `src/CMakeLists.txt` 及各功能目录的 CMakeLists 集中管理；公开头文件继续放在 `include/`，通用 `dp_*`、`co_*`、`tr_*` 实现留在 `modules/`。
- 通讯点表遵循参考工程的配置/执行分离：`src/cfg/include/` 定义信号及配置契约，`src/cfg/equipment/` 装配设备实例，`tables/` 声明三表，`protocol/` 保存地址段与回调绑定，`aidc/` 保存导入数据。`src/process/` 执行测点处理和参数命令，协议编码及 Zephyr 端口适配归 `src/communication/`；扩展步骤见 `src/cfg/README.md`。`conf/` 仅保存可选 Kconfig 配置组合。目录整理保持既有配置内容；重排持久化点或旧协议回调顺序前，必须核对 NVS 和线协议兼容性。
- 在应用目录执行 `./build.sh` 构建默认固件，或 `./build.sh --mcuboot --iapv3` 构建兼容升级配置。
- 在应用目录执行 `bash tests/run.sh` 运行完整回归；按改动范围选择必要验证。文档修改检查内容、路径和格式即可。
- 构建与测试产物放在工作区 `build/`，签名私钥放在 `build/keys/`，均不提交。
- 分开记录主机测试、模拟器验证与实物验证，不能以编译通过代替实物验证。
