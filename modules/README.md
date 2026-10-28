# 通用模块

[文档导航](../docs/README.md) · [DP](dataprocess/README.md) · [IAP](iap/README.md)

当前产品只保留本地最小核心。模块清单：

| 模块 | 接口 | 职责 |
| --- | --- | --- |
| `dataprocess` | `dp_*` | 点值、质量、单点同步读写、NVS 参数 |
| `modbus` | `modbus_support_*` | 无分配 RTU 编解码及 Zephyr Modbus 构建兼容；IAP 复用 ADU 结构 |
| `iap` | `iap_*` | IAPv3/SGZ 接收、解包、验签和结果日志 |
| `quicklz` | `qlz_decode_level3()` | SGZ 块解码 |
| `watchdog` | `watchdog_*` | 任务健康监控和外部喂狗 |
| `gd32e505` | SoC/DTS | GD32E505 时钟、ADC 和板级支持 |

`modules/modules.cmake` 只登记上述模块。原 `collect`、`transmit`、`vport`
已删除；DP 不再包含采集/转发描述符，重新接入通信需要新增独立的适配层。

## DP 使用

- `app/protocol/tables/process.c` 持有编译期只读模型。
- `app/protocol/process_runtime.c` 在 `product_tables[]` 中逐组调用 `dp_register_table()`。
- `app/acquisition/acquisition.c` 同步写本地测点。
- `app/process/points.c` 提供参数命令和状态回调。

点表行、容量和堆预算见 [dataprocess/README.md](dataprocess/README.md) 与
[内存管理](../docs/memory-management.md)。

## IAPv3

IAP 模块自身完整，但当前产品没有 TR/Modbus 路由，`iap_route()` 未被调用。
串口上传在重新加入最小路由适配层前不可用。构建、签名和分区说明见
[构建与升级](../docs/build-and-upgrade.md)。

## 构建

应用通过 `modules/modules.cmake` 在任何 `find_package(Zephyr)` 之前登记模块。
测试可直接把单个模块加入 `ZEPHYR_EXTRA_MODULES`。
