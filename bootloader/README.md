# IDU-pre MCUboot 配置

[文档导航](../docs/README.md) · [构建与升级](../docs/build-and-upgrade.md)

本目录保存 IDU-pre 的启动程序配置；MCUboot 源码仍由 West 管理，位于工作区
`bootloader/mcuboot/`，不复制到本应用仓库。

| 文件 | 职责 |
| --- | --- |
| `CMakeLists.txt` | 联合构建，选择本板根目录、镜像配置和 overlay |
| `mcuboot.conf` | 启动程序的签名、Flash 和日志配置 |
| `boot.overlay` | MCUboot 的代码分区选择，复用应用分区定义 |
| `app.conf` | 主应用启用 MCUboot / 镜像管理的配置 |
| `app.overlay` | slot0 / slot1 / scratch 布局 |

根目录 `sysbuild/CMakeLists.txt` 是 Zephyr 自动发现的薄入口，
`sysbuild.conf` 保留 Sysbuild 功能选择。使用原构建命令即可：

```bash
./applications/IDU-pre/build.sh --mcuboot --iapv3
```

`app/boot.c` 负责应用试运行确认，`app/iap/` 负责 IAP 接收、SGZ 解包及镜像校验；
这些代码运行在主应用中，和本目录的启动程序配置分开。头文件与所属功能源码同目录，
应用使用 `app/` 作为头文件搜索根，不再设置根目录 `include/` 或 `src/`。
