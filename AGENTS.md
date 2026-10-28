# IDU-pre 协作约定

## Git 工作流

- 每次完成一轮修改并执行相应验证后，自动在 `applications/IDU-pre/` 创建本地 Git 提交。
- 仅允许修改 `applications/` 下的应用仓库。工作区根目录下的 `manifest/`、`zephyr/`、`modules/`、`bootloader/` 等仓库保持只读。
- 当前项目仅在 `applications/IDU-pre/` 创建本地提交，复用现有 Git 仓库，不在工作区根目录初始化仓库。推送需用户明确要求。
- 修改前检查 `git status --short`，保留用户已有的暂存和未暂存改动；只暂存、提交当前任务的文件或修改块。
- 提交说明简洁描述行为变化，完成后报告提交哈希和验证结果。无法完成验证或提交时如实说明原因。

## 代码与构建

- 自有 C/H 使用根目录 `.clang-format`：4 空格缩进，每行最多 100 列；条件、循环、函数体和 case 分支展开，多条语句分别占行。长函数声明、调用及复杂表达式按逻辑层次换行，避免压缩成单行。Python 使用 `.ruff.toml`，同样限制 100 列；字符串可用相邻字面量分行，不改变内容。
- 修改格式时使用 clang-format / ruff format；保持逻辑、包含顺序、运行时字符串及协议编码不变。只格式化本应用自有源码，不处理生成产物、west 依赖或历史证据。
- 自有 C 枚举统一使用 `typedef enum { ... } name_t`，声明和接口使用类型别名；已存在的参考接口 typedef 名称保留。Zephyr 等外部 API 的枚举类型沿用上游定义。
- 运行时字符串、故障描述、日志和命令行／配置帮助使用英文；注释及 Doxygen 说明使用中文。
- 产品代码、板级支持和点表模块保存在本应用内；依赖版本由工作区 `manifest/west.yml` 管理。
- `modules/gd32e505` 通过 Zephyr `soc_root` / `dts_root` 提供 E505 SoC、设备树时钟配置和 ADC0 驱动；本板保持 20 MHz bypass / 180 MHz；`modules/hardware.cmake` 供应用和 Sysbuild 共用。
- 通用模块只保留 [modules/README.md](modules/README.md) 列出的最小集合。`modules/collect`、`modules/transmit`、`modules/vport` 已删除；不要恢复其头文件、Kconfig 或应用装配。
- `modules/dataprocess` 是最小原子核心，接口为 `dp_*`：模型登记、单点同步写读、本机 NVS 参数。不要恢复异步样本队列、业务线程、定时器、参考表目录、`data_process.h` 字节适配层或 collect/transmit 描述符。
- 点表行使用 `dp_point_def_t`：`FLAG_CONSTANT`、`FLAG_PERSISTENCE`、`FLAG_RANGE_CHECK`、`FLAG_READ_CALLBACK`、`FLAG_NO_RESTORE`；命令点用 `write_callback`，状态点用 `read_callback`。新增字段须同时更新 [DP 模块](modules/dataprocess/README.md) 和应用点表说明。
- DP 三个主接口为 `dp_register_table(device_id, table)`、`dp_set_para()`、`dp_get_para()`；启动线程完成登记再读写。表描述只含 points/count，可临时；行数组及引用初值须常驻只读。链表和设备点数据由模块一次分配、内部维护；同表可登记给多个设备。点值为 `dp_value_t` 联合体，运行数据仅含值及 uint32_t UNIX 秒时间（未知为 0）。启用 DP 必须提供设备树 `dp-params` 别名并指向启用的固定分区。参数维护仅保留恢复默认和状态查询，NVS 键及格式保持兼容。
- 参数写入在调用线程同步提交 NVS，没有去抖、意图日志或下游读回。`parameter_store.c` 的 NVS 记录/提交镜像格式保持兼容；不要因改点表而改变键或格式。
- `app/main.c` 启动状态、硬件和可选 IAP 后调用 `protocol_init()`，保留采样周期和休眠，终止后主线程留驻并每 5 秒查询打印最近故障。`acquisition` 只按硬件 spec 与 device_id/signal_id 映射读取 DI 位图和 ADC，并用 `dp_set_para()` 同步写点。
- `app/process/points.c` 只保留 `points_sample_period_ms()`、`points_parameter_command()` 和 `points_parameter_status()`。命令点只在数值校验通过后执行动作，不保存触发值。
- 状态与 LED 归 `app/status/`；看门狗实现在 `modules/watchdog`；IAP 归 `modules/iap`，调用方直接使用 `<iap/iap.h>`。
- IAP 模块提供 `iap_route()`，但当前最小产品没有 TR/Modbus 路由，串口上传尚不可用；不要在文档或报告中宣称 IAPv3 上传已工作。重新启用必须新增最小路由适配层并单独验证。

## 构建与验证

- 在工作区根目录使用 `./applications/IDU-pre/scripts/build.sh`。默认 MCUboot + IAPv3；`--no-upload` 关闭接收器。不要使用根目录 `./build.sh`。
- 首次构建生成签名密钥 `build/keys/IDU-pre-dev.pem`。删除 `build/` 前必须备份；`--pristine always` 会保留密钥。
- 删除或重命名配置符号后，用 `--pristine always` 重建，避免 CMake/Kconfig 缓存残留。
- 完整回归：`bash applications/IDU-pre/tests/run.sh`。当前套件覆盖 DP 核心、参数持久化、产品 process 装配、ADC、IO、Modbus 支持、看门狗、时钟和 IAP。
- 只运行与改动相关的套件。单套件示例：

  ```bash
  source .venv/bin/activate
  ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always -b native_sim/native/64 \
    applications/IDU-pre/tests/<suite> -d build/IDU-pre/tests/<suite>
  build/IDU-pre/tests/<suite>/zephyr/zephyr.exe -flash_in_ram
  ```

- 环境无法运行 LeakSanitizer 时，保留 ASan/UBSan 并使用
  `CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/run.sh`。
- 主机和模拟器结果不能证明硬件行为；主机、模拟器和实板证据分开记录。

## 文档与任务

- 对话、说明和文档使用中文；运行时字符串、日志、故障描述及 CLI/配置帮助使用英文；注释和 Doxygen 使用中文。
- 任务规格和验证记录在 `.scratch/<feature>/`（见 [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md)）。
- 当前行为文档集中在 [docs/](docs/README.md)。`docs/forward-loopback.md` 和 `scripts/forward-loopback/` 是用户未完成的改动，不要触碰或纳入本轮提交。
- 修改点表、参数键/格式、堆预算或启动顺序前，先读 [DP 模块](modules/dataprocess/README.md)、[内存管理](docs/memory-management.md) 和 [构建与升级](docs/build-and-upgrade.md)。

## Agent skills

### Issue tracker

使用本地 Markdown 记录任务和规格，通过本仓库的 Git 提交保存。
具体约定见 [docs/agents/issue-tracker.md](docs/agents/issue-tracker.md)。

### Domain docs

采用单一领域布局：仓库根目录 `GLOSSARY.md` 与 `docs/adr/`。
读取规则见 [docs/agents/domain.md](docs/agents/domain.md)。
