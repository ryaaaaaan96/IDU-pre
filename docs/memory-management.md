# 工作缓冲区与系统堆

DP 最小核心只保留两处常驻动态分配：

| 工作区 | 申请与释放 |
| --- | --- |
| 点值缓冲区 | `dp_register_table()` 每组按实际字数申请一块，服务生命周期内持有；失败返回 `-ENOMEM` |
| 参数缓存与提交镜像 | 首次访问参数接口时一次申请；存储故障时仍保留缓存，允许继续读取默认值 |

IAP 的位图、数据包、SGZ 与 Flash 写工作区由 `iap_init_config()` 一次申请，
失败时关闭 Flash 区域且不启动任务。SGZ/QuickLZ 纯算法接口仍由调用者提供工作区。

## 堆容量

所有模块共享同一个系统堆。`CONFIG_HEAP_MEM_POOL_SIZE` 为 0 时，Zephyr 使用
`HEAP_MEM_POOL_ADD_SIZE_*` 贡献之和：

| 配置后缀 | 当前预算（字节） |
| --- | ---: |
| `DATAPROCESS` | 512 |
| `DP_PARAMS` | 4096 |
| `IAP` | 12288 |
| 合计 | 16896 |

实测 `kheap__system_heap` 为 16,988 B（含分配器开销）。IAP 预算覆盖
`receiver_buffers`：512 B 镜像写入缓冲、4.25 KiB 解包工作区、1 KiB 报文、
按容器位图约 4 KiB 及扇区位图 16 B。容器上限与板级暂存分区同为 512 KiB，
改任一项须同步 `CONFIG_IAP_MAX_CONTAINER_SIZE`。

`HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS` 只需覆盖当前 24 点、61 words 的
122 B 点值及分配器开销；`HEAP_MEM_POOL_ADD_SIZE_DP_PARAMS` 覆盖
`struct param_buffers` 约 3.3 KB。新增点表宽度或持久参数时须重新核算。

`k_malloc()` 不等待，申请失败返回 `NULL`。DP 初始化失败返回 `-ENOMEM`；
参数区挂载失败只降级为 RAM 默认值，由参数状态接口报告存储错误。
IAP 分配失败关闭 Flash 区域并返回错误。

## 栈与验证

当前产品不创建 DP 样本/业务线程，DP 只在主线程和 Shell/故障上下文之外同步执行。
`CONFIG_MAIN_STACK_SIZE=4096`，`CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE=2048`；
IAP 与看门狗任务使用各自栈。实板堆峰值和栈高水位仍需另行测量。
