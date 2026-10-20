# IAP 模块

提供旧 IAPv3 的 Modbus 收包、AES 解密、SGZ/QuickLZ 解包、MCUboot
P-256 验签、Flash 暂存、NVS 结果日志和试运行升级。通过
`zephyr/module.yml`、Kconfig 和 `zephyr_library()` 接入；不依赖 IDU 源文件、
点表或 `transmit`。QuickLZ 与 MCUboot 工作区模块是构建依赖。

## IDU 配置入口

IDU 无需编写独立的 IAP 适配 `.c`。配置由以下内容提供：

| 配置内容 | 来源 |
| --- | --- |
| SGZ 暂存、结果日志分区 | [`boards/iapv3.overlay`](../../boards/iapv3.overlay) 中的 `chosen`：`iap,staging`、`iap,journal` |
| MCUboot 主／备槽 | `slot0_partition`、`slot1_partition`，布局由 [`bootloader/`](../../bootloader/README.md) 配置 |
| 镜像执行地址 | `DT_FIXED_PARTITION_ADDR(slot0_partition)`，包括 Flash 映射基址 |
| 合法栈地址范围 | `chosen` 中 `zephyr,sram` 节点的地址和大小；允许初始 SP 等于 RAM 顶端 |
| 最大签名镜像长度 | `CONFIG_IAP_IMAGE_MAX_SIZE`，包含头和 TLV，必须留足 MCUboot 交换状态／尾部空间 |
| 公钥来源 | `CONFIG_IAP_PUBLIC_KEY_FILE`，默认跟随 `CONFIG_MCUBOOT_SIGNATURE_KEY_FILE`；相对路径以应用配置目录为基准 |
| 旧 AES 传输密钥 | `CONFIG_IAP_AES_KEY_HEX`，32 个十六进制字符 |
| 升级前保存策略 | `CONFIG_IAP_REBOOT_DP_PARAMS` 或 `CONFIG_IAP_REBOOT_NO_STATE` |
| 暂存容量上限 | `CONFIG_IAP_MAX_CONTAINER_SIZE`，限制静态位图大小；实际接收限额为暂存分区大小 |

[`conf/iapv3.conf`](../../conf/iapv3.conf) 选择 DP 保存策略。模块在验签完成后调用
`dp_params_prepare_reboot()`；提交日志或请求升级失败时调用
`dp_params_cancel_reboot()`。仅选中该策略才依赖 DP；没有持久状态的其他产品
可选择 `IAP_REBOOT_NO_STATE`。IDU 的产品开关要求 DP 策略，避免配置时漏掉参数保存。

构建时从 PEM 导出公钥和哈希，签名私钥不会编入固件；AES 是旧协议互操作常量，
不替代 ECDSA 身份验证。打包工具支持 `--config <应用 .config>`；
`build.sh --mcuboot --iapv3` 自动将本次构建的 `.config` 交给它，因此修改 AES
或镜像长度限制后，收发两端使用同一份实际配置。直接运行工具时默认读取
`conf/iapv3.conf`。

## 接口与生命周期

公开头文件为 [`iap/iap.h`](include/iap/iap.h) 和
[`iap/sgz.h`](include/iap/sgz.h)。

- `iap_start()`：启用 `CONFIG_IAP_CONFIGURED` 时，按上述配置显式启动。
- `iap_init(config)`：其他使用方可以直接提供配置；结构被复制，密钥和回调上下文
  必须长期有效。可选保存／取消回调必须成对提供。
- `iap_ready()`：启动后可用于决定是否开放升级能力。
- `iap_route(iface, rx, tx)`：处理完整 Modbus ADU；0 表示回复，`-ENOENT`
  表示交给后续路由，`IAP_ROUTE_SILENT` 表示已消费且静默。
- `iap_sgz_decrypt/unpack`、`iap_image_verify`：纯 C 编解码／校验接口，支持
  调用方提供的读写回调、输出上限、AES 密钥和镜像内存策略。

应用在参数服务初始化之后、开放 Modbus 路由之前调用启动接口。模块不使用
`SYS_INIT` 提前启动，也不注册或占用串口；调用方负责站号过滤及超长 FC16 接收。
IDU 的 [`app/runtime.c`](../../app/runtime.c) 直接调用 `iap_start()`，
[`app/communication/mvio.c`](../../app/communication/mvio.c) 调用
`iap_ready()`／`iap_route()`；产品开关 `IDU_IAPV3` 归
[`app/Kconfig`](../../app/Kconfig)，不再保留旧接口别名或应用 IAP 目录。

接收器为单实例，多端口共享上传所有权；一个互斥锁、信号量和专用线程处理
有界任务，Flash 擦写和解包不阻塞 Modbus 回调。初始化失败释放 Flash 句柄并允许重试；
初始化成功后再次启动返回 `-EALREADY`。不提供运行中停机／重新配置接口。
应用健康确认仍由 [`app/boot.c`](../../app/boot.c) 执行，不属于上传协议。

## 保持的协议和限制

旧寄存器 20049–20104、1 KiB 分包、乱序补包、重复包一致性校验、广播静默、
8 分钟超时及 500 ms 重启等待保持不变。结果日志仍为原 48 字节格式，
不会将中断的未验证接收状态当作可续传状态。

当前支持单 Cortex-M ARM 子文件、QuickLZ 1.5 level 3、512 字节 MCUboot
头、SHA256、P-256 和双槽 scratch 交换；不支持多镜像、差分、下挂设备升级、
offset/RAM-load/direct-XIP。IAP 与 SMP 镜像上传不能同时启用。

暂存和日志按 4 KiB 单元管理，底层擦除页必须能整除 4 KiB；暂存写对齐不大于
16 字节，日志至少两个单元。初始化检查容量、页布局及与主／备槽的重叠。
产品仍须保证这些分区不覆盖 boot、scratch、参数等其他区域。
Flash 地址和密钥是构建配置，不允许通过上传协议修改。

## 验证

从工作区执行：

```sh
CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 bash applications/IDU-pre/tests/iap/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh --mcuboot --iapv3
```

独立模拟器测试仅加载 IAP 和 QuickLZ 模块，不编译 IDU、DP 或 transmit 源码；
覆盖收包、验签、存储和保存失败、重启日志，以及初始化失败后重试。
配置模式另用不同的 AES 密钥、执行地址、RAM 范围、镜像上限和分区大小完成升级，
并选择“无状态保存”策略。主机测试覆盖输出上限与内存策略拒绝、配置密钥互通，
另有 3 万组畸形输入 ASan/UBSan 检查。

集成方式参考 Zephyr 官方的 [Modules](https://docs.zephyrproject.org/latest/develop/modules.html)、
[Flash map](https://docs.zephyrproject.org/latest/services/storage/flash_map/flash_map.html)
和 [DFU](https://docs.zephyrproject.org/latest/services/device_mgmt/dfu.html) 文档；
实现按工作区固定的 Zephyr 4.4.2 API 验证。模拟器与构建结果不替代实板升级验证。
