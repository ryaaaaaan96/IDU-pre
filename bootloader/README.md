# IDU-pre MCUboot 配置

[文档导航](../docs/README.md) · [构建与升级](../docs/build-and-upgrade.md)

本目录保存 IDU-pre 的启动程序配置；MCUboot 源码仍由 West 管理，位于工作区
`bootloader/mcuboot/`，不复制到本应用仓库。

| 文件 | 职责 |
| --- | --- |
| `CMakeLists.txt` | 联合构建，注册硬件模块，选择本板根目录、镜像配置和 overlay |
| `mcuboot.conf` | 启动程序的签名、Flash 和日志配置 |
| `boot.overlay` | MCUboot 的代码分区选择，复用应用分区定义 |
| `app.conf` | 主应用启用 MCUboot / 镜像管理的配置 |
| `partitions.dtsi` | 板级 DTS 引用的 MCUboot / slot0 / slot1 / scratch 布局 |
| `app.overlay` | 应用选择 slot0 分区 |

根目录 `sysbuild/CMakeLists.txt` 是 Zephyr 自动发现的薄入口，
`sysbuild.conf` 保留 Sysbuild 功能选择。使用原构建命令即可：

```bash
./applications/IDU-pre/scripts/build.sh --mcuboot --iapv3
```

`modules/hardware.cmake` 在 Sysbuild 发现板与镜像之前注册
[`gd32e505`](../modules/gd32e505/README.md)，模块清单经缓存传入 MCUboot 和主应用。
SoC 根由模块元数据提供，无需把芯片路径写入镜像配置。
这里的镜像选择、签名配置和分区是 IDU 产品策略，继续保存在本目录。
`mcuboot_EXTRA_DTC_OVERLAY_FILE` 仅在未指定时设置默认值，
保留调用者的自定义值及 overlay 列表；自定义文件需包含必要的启动分区布局。
Zephyr 允许模块通过 `sysbuild-cmake` 提供共享构建逻辑；当前并无跨产品共享需求，
因此保留标准薄入口，不把整个 Sysbuild 工程装进芯片模块。

`app/status/image_confirm/image_confirm.c` 负责应用试运行确认，[`modules/iap`](../modules/iap/README.md)
负责 IAP 接收、SGZ 解包及镜像校验，通过设备树和 Kconfig 提供产品配置；
这些代码运行在主应用中，和本目录的启动程序配置分开。头文件与所属功能源码同目录，
应用使用 `app/` 作为头文件搜索根，不再设置根目录 `include/` 或 `src/`。
