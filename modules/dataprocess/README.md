# dataprocess

[文档导航](../../docs/README.md) · [模块总览](../README.md)

当前只实现点表登记、同步 RAM 读写、回调及范围检查，不包含 Flash 参数持久化。
无需 dp_init、NVS、参数分区别名或参数工作区，不创建线程、队列或定时器。

## 接口与流程

```c
int dp_register_table(uint16_t device_id, const dp_table_t *table);
int dp_set_para(uint16_t device_id, uint16_t signal_key,
                uint8_t size, const uint8_t *bytes);
int dp_get_para(uint16_t device_id, uint16_t signal_key,
                uint8_t capacity, uint8_t *bytes);
```

登记先检查入参、大小和重复键，分配私有节点及全部点数据，再逐点校验并复制配置初值。
整表成功才发布，失败释放内存、不保留半张表；失败后可重试。
设备 ID 与同表内 key 须非零、唯一；不同设备可共用一份只读点表，值相互独立。
重复键使用临时 uint16_t 数组排序检查，原表顺序保持不变。

配置行使用 dp_point_config_t，初值及上下限为公共 value_t，统一内嵌 u16/words/bytes 等成员。
不得将 initial.array 用作外部初值引用；行数组须 static const 或具有同等常驻只读生命周期。
dp_table_t 描述本身可以临时存在。运行数据为值和 uint32_t updated_at_s。
单点容量 CONFIG_COMMON_VALUE_WORDS，系统堆预算 HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS。

写入：解码 → 写回调（可选）→ 校验最终值 → 保存 RAM 和时间。
读取：复制当前值 → 读回调（可选）→ 校验并保存 RAM → 编码返回。
没有读回调时仅复制，不更新时间。回调失败或最终值越界时不改变点值、时间及读输出。
写长度须完全匹配；读容量须足够，成功返回传入容量；字节编码低字节在前。
任何修改重启后均恢复配置初值，不会加载或写入原参数 Flash 分区。

## 回调与索引

读写使用同一 dp_callback_t(const dp_callback_context_t *)，上下文包含 device_id、
signal_key、signal_index、bool is_write 和可修改的 value 指针。
index 是注册表中从零开始的行下标，对应 process 枚举；key 是稳定标识。
身份只读，value 可修改，上下文和指针只在同步回调期间有效，不能长期保存。
回调在锁外运行，可以访问其他点；同点并发回调不是原子读改写。

## 校验与并发

只保留 DP_POINT_FLAG_CHECK_RANGE，数值类型启用时使用配置上下限。
非数值类型设置此标志时登记警告并忽略；未启用时上下限静默忽略。
类型长度不匹配、零长度、超容量、奇数 WORDS、非法类型、初值越界等拒绝登记。
支持 U8/U16/S16/U32/S32、STRING/BYTES/WORDS，f32 仅公共联合体视图，尚无浮点点表支持。

登记在启动线程独占完成，随后可以并发读写；ISR 返回 -EWOULDBLOCK。
只保留一把点值互斥锁，保护值及时间的完整快照，不在锁内执行回调。
时间为 UNIX UTC 秒，未校时、初值或超出 32 位范围时为 0，不持久保存。
系统墙钟偏移非零表示已校时；超时判断应使用 monotonic 时钟。

## 后续功能

参数持久化、恢复默认命令、存储状态及 IAP 的 DP 保存门禁已移除，后续按需求重新设计。
板级原参数分区及其历史数据未擦除；IAP 自身的 NVS 升级日志仍独立保留。
