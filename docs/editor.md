# VS Code 与 clangd

本应用通过根目录 `.clangd` 指定当前 MCUboot + IAPv3 应用镜像的编译数据库：

```text
build/IDU-pre/mcuboot-iapv3/IDU-pre/compile_commands.json
```

此路径相对于 West 工作区；`.clangd` 内使用相对于配置文件的路径。
直接打开整个 West 工作区或应用目录，都可以使用同一份配置。
路径和编译选项处理遵循 [clangd 项目配置](https://clangd.llvm.org/config)。

首次使用或清理构建后，先在工作区根执行：

```bash
./applications/IDU-pre/scripts/build.sh
```

构建生成真实的编译命令、设备树头文件及 `autoconf.h`，供 clangd 解析。
Sysbuild 顶层和 MCUboot 子目录的数据库不能替代应用镜像的数据库。
切换到 SMP 或无上传入口的构建时，手动将 `.clangd` 的 `CompilationDatabase`
调整为对应构建的应用镜像目录，再重启语言服务。

VS Code 安装并启用 `llvm-vs-code-extensions.vscode-clangd`。
配置修改后执行命令面板中的 `clangd: Restart language server`，等待后台索引完成，
再使用 F12 或 Ctrl+单击。初次索引时，跳转可能暂时只能到声明。

`.clangd` 只在编辑器解析时移除三个 GCC 专用参数，实际固件编译命令保持原样。
本机 Zephyr SDK 的标准头文件由数据库中的编译器路径和 sysroot 正常识别，无需额外写死 SDK 路径。
若自定义工具链缺少系统头文件，可依据 [clangd 系统头文件说明](https://clangd.llvm.org/guides/system-headers)
检查工具链路径及 `--query-driver` 配置。

排查时在 clangd 输出中确认读取了上述数据库，且没有使用 `Generic fallback command`。
避免在个人 clangd 参数中固定旧的 `--compile-commands-dir`；应与当前应用构建一致。
