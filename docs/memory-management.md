# 工作缓冲区与系统堆

DP 点状态改为每张表静态持有，登记不分配内存：

| 工作区 | 申请与释放 |
| --- | --- |
| 表描述和点状态 | 应用静态定义，状态数组长度由点表行数确定；服务生命周期内持有 |
| 参数缓存与提交镜像 | 首次访问参数接口时一次申请，保留到重启 |

当前 24 个点每个状态 56 B，共 1344 B；包含每点 16 words 的完整值数组。
移除原 slots（32 B）、全局 states（768 B）和点值堆预算（512 B）。
参数枚举直接引用行定义，不再通过全局点下标查找。

IAP 的位图、数据包、SGZ 与 Flash 写工作区由 `iap_init_config()` 一次申请，
失败时关闭 Flash 区域且不启动任务。SGZ/QuickLZ 纯算法接口仍由调用者提供工作区。

## 堆容量

所有模块共享同一个系统堆。`CONFIG_HEAP_MEM_POOL_SIZE` 为 0 时，Zephyr 使用
`HEAP_MEM_POOL_ADD_SIZE_*` 贡献之和：

| 配置后缀 | 当前预算（字节） |
| --- | ---: |
| `DP_PARAMS` | 4096 |
| `IAP` | 12288 |
| 合计 | 16384 |

2026-10-28 默认 MCUboot + IAPv3 构建测得 `kheap__system_heap` 为 16,476 B（含分配器开销）。IAP 预算覆盖
`receiver_buffers`：512 B 镜像写入缓冲、4.25 KiB 解包工作区、1 KiB 报文、
按容器位图约 4 KiB 及扇区位图 16 B。容器上限与板级暂存分区同为 512 KiB，
改任一项须同步 `CONFIG_IAP_MAX_CONTAINER_SIZE`。

`HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS` 覆盖 `struct param_buffers` 约 3.3 KB。
新增点表或调整 `DP_POINT_WORDS` 时，静态 RAM 用量随点数和单点最大宽度增长。

本轮默认固件静态 RAM 为 40,856 B，较改动前 40,760 B 增加 96 B；
Flash 为 87,932 B，较改动前 88,756 B 减少 824 B。它们是链接结果，不代表实板峰值。

`k_malloc()` 只用于参数和 IAP 工作区，失败返回错误；应用读取采样周期失败时
使用默认值。IAP 分配失败关闭 Flash 区域并返回错误。

## 栈与验证

当前产品不创建 DP 样本/业务线程，DP 只在主线程和 Shell/故障上下文之外同步执行。
`CONFIG_MAIN_STACK_SIZE=4096`，`CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048`；
IAP 与看门狗任务使用各自栈。实板堆峰值和栈高水位仍需另行测量。
