# dataprocess

[文档导航](../../docs/README.md) · [模块总览](../README.md)

当前只实现点表登记、同步 RAM 读写、回调及范围检查，不包含 Flash 参数持久化。
无需 dp_init、NVS、参数分区别名或参数工作区，不创建线程、队列或定时器。

对外统一包含 `<dataprocess/dataprocess.h>`，配置类型、回调上下文和三个主接口均在其中声明。

## 接口与流程

```c
int dp_register_table(uint16_t device_id, const dp_table_t *table);
int dp_set_para(uint16_t device_id, uint16_t signal_key, const value_t *value);
int dp_get_para(uint16_t device_id, uint16_t signal_key, value_t *value);
```

登记先检查入参、大小和重复键，分配私有节点及全部点数据，再逐点校验并复制配置初值。
整表成功才发布，失败释放内存、不保留半张表；失败后可重试。
设备 ID 与同表内 key 须非零、唯一；不同设备可共用一份只读点表，值相互独立。
重复键使用 Zephyr OA/LP hashmap 作为临时 key 集合，原表顺序保持不变。
每次检查完成（含错误路径）清空全部桶，分配来自系统堆 k_realloc，不使用 libc malloc。

配置行使用 dp_point_config_t，初值及上下限为公共 value_t，统一内嵌 u16/words/bytes 等成员。
不得将 initial.array 用作外部初值引用；行数组须 static const 或具有同等常驻只读生命周期。
dp_table_t 描述本身可以临时存在。运行数据为值和 uint32_t updated_at_s。
单点容量由 CONFIG_COMMON_VALUE_WORDS 决定，系统堆预算 HEAP_MEM_POOL_ADD_SIZE_DATAPROCESS。

写入：复制 value_t → 写回调（可选）→ 校验最终值 → 保存 RAM 和时间。
读取：复制当前值 → 读回调（可选）→ 校验并保存 RAM → 返回 value_t。
没有读回调时仅复制，不更新时间。回调失败或最终值越界时不改变点值、时间及读输出。
读写成功均返回 0，失败返回负错误码；传入空指针返回 -EINVAL。
调用方须初始化整个 value_t 并按点表类型填写成员，DP 不负责字节序或线协议编码。
写入使用副本，回调修改不影响调用方输入；读取成功才覆盖输出。
任何修改重启后均恢复配置初值，不会加载或写入原参数 Flash 分区。

## 回调与索引

读写使用同一 dp_callback_t(const dp_callback_context_t *)，上下文包含 device_id、
signal_key、signal_index、bool is_write 和可修改的 value 指针。
index 是注册表中从零开始的行下标，对应 process 枚举；key 是稳定标识。
身份只读，value 可修改，上下文和指针只在同步回调期间有效，不能长期保存。
回调在锁外运行，可以访问其他点；同点并发回调不是原子读改写。

## 校验与并发

DP_POINT_FLAG_CHECK_RANGE 控制数值上下限检查。
DP_POINT_FLAG_SKIP_RESTORE 控制显式恢复默认时跳过该点，不影响登记初始化。
非数值类型设置此标志时登记警告并忽略；未启用时上下限静默忽略。
类型长度不匹配、零长度、超容量、奇数 WORDS、非法类型、初值越界等拒绝登记。
支持 U8/U16/S16/U32/S32、STRING/BYTES/WORDS，f32 仅公共联合体视图，尚无浮点点表支持。

登记在启动线程独占完成，随后可以并发读写；ISR 返回 -EWOULDBLOCK。
只保留一把点值互斥锁，保护值及时间的完整快照，不在锁内执行回调。
时间为 UNIX UTC 秒，未校时、初值或超出 32 位范围时为 0，不持久保存。
系统墙钟偏移非零表示已校时；超时判断应使用 monotonic 时钟。

## 后续功能

参数持久化、产品恢复默认命令点、存储状态及 IAP 的 DP 保存门禁已移除，后续按需求重新设计。
板级原参数分区及其历史数据未擦除；IAP 自身的 NVS 升级日志仍独立保留。

## RAM 恢复默认

调用 dp_restore_defaults(device_id)，将目标设备所有未设置 SKIP_RESTORE 的点
恢复为配置初值并记录当前 UNIX 秒；被跳过点的值和时间保持不变，不影响其他设备。
仅在线程使用，设备不存在返回 -ENOENT，设备 ID 为零返回 -EINVAL。
恢复直接复制已校验的初值，不调用读写回调，不增加参数存储或额外锁。
并发回调仍采用最后提交者覆盖语义；恢复默认不会取消已经在执行的回调。

零值配置使用 C 静态初始化规则，不再提供 DEFAULT_VALUE/FLAGS/CALLBACK/ARRAY 宏。
点表不填写未启用的上下限；容量直接检查公共 value_t 内嵌 bytes 成员长度。

登记 hash 集合平均插入 O(1)，整体平均 O(n)；桶使用 uint64_t key/value，
内存明显大于原 uint16_t 副本。扩容时新旧桶同时存在，需按峰值调整系统堆。
512 行测试显式增加测试堆，产品当前 21 点仍使用原 2048 B DP 堆预算。
静态描述符只用于启动串行登记，桶在结束后释放，不参与运行期查找。

例如 `value_t value = {.u16 = 1234}; dp_set_para(device_id, key, &value);`。
读回时传入 `value_t *`，通过配置类型对应的成员访问。数组使用内嵌 bytes/words，
有效长度仍由点配置 size 决定；协议接入层负责外部报文与本机数值的转换。
