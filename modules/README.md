# 通用模块

[文档导航](../docs/README.md) · [DP](dataprocess/README.md) · [IAP](iap/README.md)

当前产品模块清单：

| 模块 | 接口 | 职责 |
| --- | --- | --- |
| `common` | `<common/value.h>`、`<common/scale.h>` | 共享数值类型及比例偏移整数换算 |
| `dataprocess` | `dp_*` | 设备登记、RAM 点值、回调与NVS 延后保存 |
| `vport` | `vport_*` | 同步交付字节与接收事件，回调输出 |
| `transmit` | `tr_register()` | 固定缓存、锁定轮转、从机映射与协议放缩 |
| `modbus` | `modbus_support_*` | RTU 编解码、Zephyr RAW 服务端同步适配及公共寄存器转换；不依赖 DP |
| `iap` | `iap_*` | IAPv3/SGZ 接收、解包、验签和结果日志 |
| `quicklz` | `qlz_decode_level3()` | SGZ 块解码 |
| `watchdog` | `watchdog_*` | 任务健康监控和外部喂狗 |
| `gd32e505` | SoC/DTS | GD32E505 时钟、ADC 和板级支持 |

`modules/modules.cmake` 只登记上述模块。transmit/vport 已按新设计重新实现，collect 仍未恢复；DP 不包含通信描述符。
APP 的 protocol/protocol.c 装配协议并创建线程，rs485_1.c 提供物理收发与事件采集；模块不创建通信线程。

## DP 使用

- `app/protocol/tables/process.c` 持有编译期只读模型。
- `app/protocol/protocol.c` 先调用 `dp_init()`，再调用 `dp_register_device(device_id, table)`，私有目录及点数据由 DP 维护。
- `app/acquisition/acquisition.c` 同步写本地测点。
- `app/process/points.c` 读取应用采样周期。

点表行、容量和堆预算见 [dataprocess/README.md](dataprocess/README.md) 与
[内存管理](../docs/memory-management.md)。

## IAPv3

IAP 模块通过产品 forward.c 的完整 ADU 地址段回调接入 transmit，复用 iap_route()。
模拟器已验证完整升级链路；旧上位机实板上传仍待验证。构建、签名和分区说明见
[构建与升级](../docs/build-and-upgrade.md)。

## 构建

应用通过 `modules/modules.cmake` 在任何 `find_package(Zephyr)` 之前登记模块。
测试可直接把单个模块加入 `ZEPHYR_EXTRA_MODULES`。
