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
- 通用模块以 [modules/README.md](modules/README.md) 为准。transmit/vport 按 [通信设计](docs/transmit-design.md) 重新实现；collect 仍未恢复，不恢复旧通信框架。修改接收、协议轮转、放缩或串口归属前先读 [transmit 契约](modules/transmit/README.md) 和 [产品端口](app/protocol/README.md)。
- `modules/dataprocess` 是最小原子核心，接口为 `dp_*`：点表登记、单点同步 RAM 写读、回调和范围检查。不要恢复异步样本队列、业务线程、定时器、参考表目录、`data_process.h` 字节适配层或 collect/transmit 描述符。
- Modbus 功能码与标准响应复用 Zephyr RAW 服务端；同步适配和整笔访问接口在 modules/modbus，DP 访问与转发表在 transmit。RAW 默认异步，本项目的固定版本构建期扩展保持 APP 线程同步执行；修改前读 modules/modbus/README.md。
- common/scale.h 提供 value_scale_t 和整数换算；转发表 scale 指针为 NULL 时跳过，不维护独立放缩标志。
- 点表行使用 dp_point_config_t，保留 DP_POINT_FLAG_CHECK_RANGE 和 DP_POINT_FLAG_SKIP_DEFAULTS；回调结果校验成功后保存 RAM。配置行数组常驻只读，初值为内嵌 value_t。
- DP 接口为 dp_init、dp_register_device/remove_device、dp_set_para_by_key/get_para_by_key、dp_set_para_by_index/get_para_by_index、dp_restore_defaults、dp_factory_reset 和 dp_flush。回调在锁外，点值使用快照锁；登记/删除与保存共用设备锁，调用方须先停止并等待全部 DP 业务访问结束。
- DP 始终包含参数持久化能力，要求 dp-params 别名。详情见 [DP 模块](modules/dataprocess/README.md)：dp-params 总分区内部管理 NVS 与设备槽位，5 秒保存线程、dirty/已保存值比较、阻塞 flush。set 成功只表示 RAM 更新；重启前由应用停止修改并 flush。IAP 自身 NVS 独立，不恢复旧 DP 门禁。
- `app/main.c` 启动状态、硬件和可选 IAP 后调用 `protocol_init()`；protocol.c 登记 DP、装配转发实例并创建协议线程；rs485_1.c 只负责物理收发与事件采集。保留采样周期和休眠，终止后主线程留驻并每 5 秒查询打印最近故障。`acquisition` 只按硬件 spec 与 device_id/signal_key 映射读取 DI 位图和 ADC，并用 `dp_set_para_by_key()` 同步写点。
- `app/process/points.c` 提供采样周期读取以及参数保存、操作命令写回调；保持寄存器 16 写 1 同步 flush，17 为非持久对照值，18 为操作命令（1 冷重启、2 恢复默认值、3 清除全部 DP 参数后重启）。
- 状态与 LED 归 `app/status/`；看门狗实现在 `modules/watchdog`；IAP 归 `modules/iap`，调用方直接使用 `<iap/iap.h>`。
- IAP 通过 forward.c 的完整 ADU 地址段回调接入 transmit，在标准功能码校验前调用 `iap_route()`，保留旧超长 FC16。修改上传路由前读 modules/iap/README.md；模拟器组合测试已通过，实板上传及旧上位机互通仍待验证。

## 构建与验证

- 在工作区根目录使用 `./applications/IDU-pre/scripts/build.sh`。默认 MCUboot + IAPv3；`--no-upload` 关闭接收器。不要使用根目录 `./build.sh`。
- 首次构建生成签名密钥 `build/keys/IDU-pre-dev.pem`。删除 `build/` 前必须备份；`--pristine always` 会保留密钥。
- 删除或重命名配置符号后，用 `--pristine always` 重建，避免 CMake/Kconfig 缓存残留。
- 完整回归：`bash applications/IDU-pre/tests/run.sh`。当前套件覆盖 DP 的 RAM 核心、产品 process 装配、ADC、IO、Modbus 支持、看门狗、时钟和 IAP。
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

## 文档维护

- 当前说明集中在 [docs/README.md](docs/README.md)，模块 API 与生命周期以模块 README 为准。
- 修改点表、参数、堆或启动顺序前，读取 DP 模块、docs/memory-management.md 和 docs/build-and-upgrade.md。
- 当前实现直接更新对应主题；验证范围记录在 docs/testing.md，区分模拟器和实板结果。
- 设计讨论和中间任务不保留为正式文档；.scratch 仅作临时草稿，任务完成后清理。
- 领域术语沿用根目录 GLOSSARY.md。历史方案通过 Git 查询，不恢复过程 ADR 或重复台账。
- docs/forward-loopback.md 和 scripts/forward-loopback/ 是保留的用户调试资料，专项修改时再处理。
- HTML 是设计报告源文件，更新后运行 python3 scripts/docs/export_module_report.py 同步 Word。
