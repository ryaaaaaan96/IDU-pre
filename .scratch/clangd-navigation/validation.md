# clangd 跨文件跳转修复验证

## 原因及修复

应用源码目录的祖先中没有可用编译数据库；当前数据库实际位于 Sysbuild 的应用镜像子目录。
未配置时 clangd 使用 fallback，无法取得完整的 Zephyr 头文件、设备树和配置宏，跳转只能定位声明。
显式指定数据库后仍出现三项 GCC 参数错误：-fno-printf-return-value、-mfp16-format=ieee、-fno-reorder-functions。

应用根 .clangd 指向现用 MCUboot + IAPv3 的应用数据库，并仅对编辑器移除这三项参数。
没有修改根工作区的 VS Code 配置、上游仓库、固件构建参数、编译数据库或全局用户设置。
本机工具链头文件无需 query-driver 即可解析。

## 复现与验证

- 使用 VS Code 远程配置实际选择的 clangd 23.1.0，通过标准 LSP didOpen 和 textDocument/definition 请求验证。
- 关闭项目配置、等待 10 秒索引后，main.c 的 protocol_init 仍仅返回 protocol.h 声明，未找到实现，复现失败信号。
- 启用修复后七处跳转全部通过：protocol_init、acquisition_cycle、status_init、dp_init、input_sampler_read、points_parameter_command，以及上游 GPIO 内联实现 gpio_pin_configure_dt。
- 六个打开文件收到的 LSP 诊断中错误数为 0。
- 不传 --enable-config 等额外参数时，clangd --check=applications/IDU-pre/app/status/status.c 自动发现 .clangd，返回 All checks completed, 0 errors。
- main.c 的 --check 仍有 clangd 自带 SwapBinaryOperands 重构测试的重叠替换错误；这是 K_MSEC 附近的内部功能自检，LSP 无对应源码诊断，七处实际跳转均正常。没有屏蔽源码诊断来规避它。
- 诊断期间使用 XDG_CACHE_HOME 将测试进程的缓存放到 build/，避免执行环境限制用户缓存写入。

临时 LSP 验收脚本及日志保存在 build/IDU-pre/clangd-validation/，均不提交。
未直接操作 VS Code 图形界面；用户在当前窗口重启 clangd 后由编辑器加载该配置。
