# 内存与容量配置

[文档导航](README.md) · [DP 模块](../modules/dataprocess/README.md) · [验证说明](testing.md)

## 系统堆

默认 IAPv3 产品配置使用共享系统堆，预算贡献如下：

| 配置 | 字节数 |
| --- | ---: |
| HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS | 4096 |
| HEAP_MEM_POOL_ADD_SIZE_IAP | 12288 |
| HEAP_MEM_POOL_ADD_SIZE_TRANSMIT | 16384 |
| 合计 | 32768 |

预算是容量上限，不等于实际占用。增加设备、点数、接收容量或容器上限后，需要同时检查启动峰值。

## DP 设备与点值

只读配置由 APP 常驻持有。每个设备一次申请私有节点、全部点运行数据及持久点保存副本。
申请量为 `sizeof(dp_registered_table_t) + 点数 × sizeof(dp_point_data_t)`，
再加持久点数乘以 `sizeof(value_t)`；分配器和可选设备 Hash 索引开销另计。

COMMON_VALUE_WORDS=16 时，value_t 为 32 B。当前 ARM 配置点运行数据 44 B、
设备头 24 B，24 点与 3 个持久副本共 1176 B，不含分配器开销。
登记时还临时申请重复 key 检查的 Hash 桶，预算应覆盖已有设备与扩容峰值。
具体布局以当前编译器的 sizeof 为准。

持久点的 saved_value 仅为最后保存快照，saved_valid 表示快照可信，dirty 表示需要写回。
普通点不申请保存副本。点值锁不跨 Flash 操作；后台保存与生命周期使用设备锁。

## NVS 与 Flash

DTS 的 dp-params 指定总区域，DP_STORAGE_SLOT_COUNT 指定持久设备槽位数，默认 6。
管理区和各设备槽位分别使用 NVS，IAP 日志使用独立 NVS。
NVS lookup cache 的 RAM 开销随实例数和配置增加；记录寻址由 NVS 管理。
槽位数量或布局变化会触发 DP 参数区清空重建，详见模块存储契约。

## 通信缓存

transmit 每实例启动时申请实例头、每接收字节的 uint64_t 时间戳、接收缓存与发送缓存，
运行期不扩容。默认 IAP 构建接收容量 1100 B，发送容量 256 B；不含 IAP 时接收容量 256 B。

APP 另有 256 项带接收时间的 ISR 消息队列和 8192 B 协议线程栈。
当前仅配置 1 个 RAW 接口；增加接口会增加官方收发 ADU 与每实例 250 B 业务暂存。
IAP 的位图、数据包和解包工作区在初始化时申请，与 transmit 缓存分别预算。

## 线程栈与实测

主线程 4096 B，系统工作队列 2048 B，DP 保存线程默认 2048 B；
协议和 IAP 线程各自配置。watchdog 的检查使用系统工作队列，硬件喂狗使用 k_timer。
当前产品不启用官方 Modbus 专用工作队列。

可使用 Shell 的 `kernel heap`、`kernel thread stacks` 辅助观察。
最新链接容量见[验证说明](testing.md#固件与资源)；运行期栈高水位、堆峰值、
Flash 阻塞时间及持续满速串口负载仍需实板记录。
