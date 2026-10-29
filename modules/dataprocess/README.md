# dataprocess

[文档导航](../../docs/README.md) · [ADR 0015](../../docs/adr/0015-dp-device-storage.md)

## 接口及生命周期

统一包含 `<dataprocess/dataprocess.h>`：

```c
int dp_init(void);
int dp_register_device(uint16_t device_id, const dp_table_t *table);
int dp_remove_device(uint16_t device_id);
int dp_set_para(uint16_t device_id, uint16_t signal_key, const value_t *value);
int dp_get_para(uint16_t device_id, uint16_t signal_key, value_t *value);
int dp_restore_defaults(uint16_t device_id);
int dp_factory_reset(uint16_t device_id, dp_factory_reset_callback_t before_reboot, void *context);
int dp_flush(void);
```

先 init，再逐设备登记，最后启动业务。重复成功 init 返回 0，重复登记返回 -EEXIST。
登记/删除由调用方串行执行，并停止、等待全部 DP 业务调用（含其他设备和回调）结束；
模块设备锁只保护目录与后台保存，不替代业务生命周期管理。
ISR 调用返回 -EWOULDBLOCK。缺少 init 时登记、删除、flush 返回 -EACCES。

配置行数组须常驻只读；描述本身可临时。同表可用于多个 device_id，值独立。
登记检查重复键；第一遍校验配置及初值，同时统计持久点。
随后分配私有节点，仅含持久点时挂载设备存储；第二遍设置初值、关联保存副本并加载、校验历史值。
缺失或不兼容记录、越界历史值使用已校验初值，同步追加记录，成功后继续登记。
全部成功才发布；失败释放本次节点。登记可能已追加前面测点，重试时直接读取，不是整表事务。
已分配的存储槽位不因登记失败回收，下次重试仍使用它。
临时 OA/LP hashmap 检查同表重复 key，结束释放桶；运行期索引由 DP_HASH_LOOKUP 决定：
默认设备线性查找，启用时仅设备用 hash，测点仍线性查找。设备链表头插，无遍历顺序约定。

## RAM、回调和校验

调用者初始化整个 value_t，并按点类型填写对应成员；DP 不解释线协议字节序。
set 复制输入 → 写回调 → 校验 → 更新 RAM 和时间；持久点与最后保存值比较，更新 dirty。
get 无读回调时纯复制；有回调则操作快照副本，校验后更新 RAM，变化同样标记 dirty。
失败不修改调用方输入/读取输出；回调副作用不能回滚。所有成功接口返回 0。

回调上下文含 device_id、signal_key、原行 signal_index、is_write 和可修改 value。
上下文和指针仅同步回调期间有效；回调不持有设备锁或点值锁，可访问其他点。
同点并发回调仍为最后成功提交者覆盖，不提供原子读改写。

类型/长度在登记时校验，初值和加载值都检查范围。支持 U8/U16/S16/U32/S32、
STRING/BYTES/WORDS；公共 f32 视图不代表 DP 浮点支持。非数值范围标志警告并忽略。
时间为 uint32_t UNIX UTC 秒；未校时、加载/初值、超出范围记 0，不保存到 Flash。

## 配置入口

- DTS 的 dp-params：Flash 起始地址和总区域大小。
- Kconfig 的 DP_STORAGE_SLOT_COUNT：预留几个持久设备，默认 6。
- Kconfig 的 DP_SAVE_INTERVAL_MS / DP_SAVE_STACK_SIZE / DP_SAVE_PRIORITY：保存线程运行参数。
- 点表 PERSISTENT：哪些点保存；纯 RAM 设备不占槽位。

params.c 顶部统一计算布局：PARAMS_SIZE → 扣除布局及管理区 → AVAILABLE_SECTORS
→ 除以 SLOT_COUNT → SLOT_SECTORS → SLOT_SIZE。余下不足一槽的扇区保持预留。
这些布局量是编译期常量，运行期只维护设备归属、挂载状态和每槽映射错误。

## 持久化配置与布局

启用 DATAPROCESS 就启用持久化能力，必须提供 dp-params 设备树分区；没有独立开关。
模块启用 NVS 数据 CRC 和 lookup cache；纯 RAM 设备仍不占用持久槽位。
原 signal_key 0xffff 为 NVS 保留 ID，不允许用于持久点；普通点仍可使用。

总区域内部布局：

1. 首 4 KiB：固定 32 字节布局标记（含版本、槽位大小/数量、区域大小、CRC 配置和 CRC）。
2. 随后 8 KiB：管理 NVS，每个槽位一条 device_id/状态映射。
3. 后续：固定大小设备槽位，每槽位一个 NVS；剩余不足一槽的空间不使用。

槽位数量由 CONFIG_DP_STORAGE_SLOT_COUNT 配置，默认 6，范围 1–255。
每槽扇区数 = (总区域大小 − 12 KiB) / 4096 / 槽位数量，向下取整；每槽大小由此计算。
每 NVS 至少两个 4 KiB 扇区，空间不够时编译报错；不再配置槽位字节数。
64 KiB 总区域配置 2/3/6 槽时，每槽分别为 24/16/8 KiB。
布局读取成功但标记与当前配置不匹配时，擦除整个 dp-params 总区域并写入新标记。
首次使用的空白区同样按此流程建立布局；已有映射和参数不迁移，登记时重新使用配置初值。
管理映射的设备 ID 为零、设备重复、版本/长度/状态非法时，记录原因，整区重建后重新加载一次。
重新加载仍非法就返回 -EBADMSG，不无限重试。单点参数记录错误不触发整区清理。
Flash/NVS 读取失败直接返回错误，不作为内容非法处理，不触发擦除。
擦除或写入失败直接返回错误，不继续挂载。
布局标记写入中断后，下次启动若仍不匹配会再次擦除重建。
擦除只限 dp-params，不涉及旧参数保留区、IAP 日志和镜像。
NVS 坏区自动初始化不应启用；本模块不启用 NVS_INIT_BAD_MEMORY_REGION。

设备映射持久保存，与登记顺序无关。纯 RAM 设备不占槽位。
新设备复用 device_id=0 的查找来选择空槽，擦除后与已有设备共用一条配置/挂载流程，
挂载成功才提交新映射。已有设备不擦除、不重写归属。无空槽返回 -ENOSPC。
attach 不扫描点表；调用方使用登记时已有的持久点计数决定是否调用，保留 ID 在配置校验时拒绝。
映射写入错误只在对应槽位记录 mapping_error，并保留其 device_id，避免被视为空闲。
该设备再次挂载、删除返回原错误；其他槽位可继续登记、保存和删除。
已登记设备的 RAM 读写及参数保存继续可用。mapping_error 仅在 RAM，重启重新读取实际映射，
已提交的删除标记会继续清理；从未提交的新映射则重新成为空闲槽位。
原设备正常 RAM 操作及保存不依赖新的槽位分配。

每点一条记录，NVS ID 即 signal_key。记录是固定版本/类型/长度头和小端有效载荷；
不保存 C 结构体填充、指针、更新时间、行索引或整个联合体。
类型编码由 common/value.h 的 value_storage_type() 统一提供，不依赖 value_type_t 数字。加载不调用回调。
缺失记录（-ENOENT）使用初值并同步追加；记录头版本/类型/长度不兼容或历史值越界时，
警告并使用初值，同步追加新记录替代旧值。其他 Flash/NVS 读取错误（含 CRC 错误）直接返回，
不作为缺失或不兼容处理，不覆盖原记录。追加失败则登记失败，可重试。

删除点或移除 PERSISTENT 不清理历史；旧 key 不复用于其他含义。
恢复同 key/类型/长度会读回历史值。整设备永久删除才回收区域。

## 延后保存与 flush

dp_point_data_t 仅定义于 src/dp_internal.h，不作为对外类型公开；平铺保存当前值、时间、dirty、saved_valid 和 value_t *saved_value。
只为持久点分配 value_t 保存副本，普通点的 saved_value 为空，不再使用额外包装结构体。
副本紧跟设备点数据，在同一次内存分配内，删除设备时一起释放。
读取历史记录或追加初值成功后初始化保存副本，saved_valid=true。
保存结果不确定时 saved_valid=false，旧副本保留但不能用于取消待保存状态。
NVS 追加写和回收会搬移记录，地址加速由各 nvs_fs 内的 lookup cache 管理。
DP_SAVE_INTERVAL_MS 默认 5000：独立 dp_save 线程等待间隔、调用 flush，再继续等待。
它不是严格固定周期；保存耗时、调度延迟和失败会延长未落盘窗口。
线程栈/优先级由 DP_SAVE_STACK_SIZE / DP_SAVE_PRIORITY 配置。

set 成功只表示 RAM 更新。保存只扫描 dirty 点，复制当前值后释放点值锁再写 Flash；
成功后更新已保存副本，再比较最新 RAM 值决定 dirty。保存期间的新修改不会漏写，
即使 RAM 在保存期间回到旧 Flash 值，完成后也会根据实际保存的快照重新比较。
失败不更新保存副本，将 saved_valid 清零并重新置 dirty、记录英文日志并重试。
保存副本可信时，保存前将值改回已保存值可直接清 dirty；副本失效时保留 dirty，
即使后续 set、读回调或默认恢复改回旧副本，也必须重试至保存成功。
相同有效字节不标记新的修改，NVS 也会抑制相同记录写入。

flush 同步阻塞，与后台保存串行，逐点尝试保存并返回第一个错误。
保存期间允许业务修改，故成功不保证并发新值全部落盘；主动重启/升级前由应用先停止修改再 flush。
不自动在 watchdog/reset 或 IAP 中增加阻塞保存，突然掉电仍可能丢失未保存修改。

锁顺序为设备/保存锁 → 点值锁；普通 set/get 仅短暂持有点值锁，不等待 Flash。
后台遍历与登记/删除/出厂恢复用同一设备锁，防止保存访问已释放节点。

## 恢复默认及设备删除

restore_defaults 更新指定设备 RAM 初值和时间，持久点变化标脏；
SKIP_DEFAULTS 点保留值/时间，不调用回调。需要立即落盘时再 flush；多点保存非整体事务。

factory_reset 无需停止 RAM 读写；与后台保存共用设备锁，等待已进行的保存结束后暂停目标设备保存，
持久标记删除中，擦除整个设备槽位并删除映射；不考虑 SKIP_DEFAULTS，保留 RAM 点表至重启。
成功释放设备锁，调用可选 before_reboot(context)，随后 sys_reboot(SYS_REBOOT_COLD)。
回调在线程上下文执行，须返回，不得登记/删除设备；异步日志不保证发送完成。
失败不回调、不重启，目标设备保持禁止保存，RAM 读写及默认恢复继续可用，可重试出厂恢复；断电后启动继续已提交的删除。
恢复过程中不得并发登记或删除设备；回调及普通业务读写不需要设备锁。
其他设备存储不受影响，纯 RAM 设备也可以出厂恢复并重启。
单设备恢复不要求已登记：存在 RAM 点表时暂停保存，否则直接清理历史 Flash 槽位。
没有历史槽位也视为清理成功，仍执行可选回调并重启；真实存储错误仍返回失败。
DP_DEVICE_ID_ALL（0）仅在 factory_reset 中表示全部设备：暂停所有设备的保存，直接擦除
整个 dp-params（含映射、未登记设备历史值和布局头），下次启动自动重建；RAM 点表仍保留。
普通登记仍不允许设备 ID 为 0，单设备恢复语义不变；IAP 和镜像区域不受影响。
DP 启用时选择 Zephyr REBOOT，不通过应用 status/watchdog 安排二次重启。

remove_device 不保存 dirty 值，而是永久删除整个设备：先持久标记删除中，
由 dp_params_remove 完成槽位擦除和映射删除，成功后再移除设备索引/链表并释放私有 RAM。
失败保留 RAM 并暂停该设备保存，槽位不可复用；RAM 读写继续可用。
再次调用或下次 init 完成中断删除；不存在返回 0，成功后可重新登记并使用初值。
不释放应用只读点表，其他设备不受影响。不要在正在执行的回调中登记/删除设备。

## 产品与测试

产品仅采样周期、标签和保存地址持久化；DI/AI、状态及产品标识仍是 RAM 点。
板级新区域为 0x1c000/64 KiB：标记 4 KiB、管理 8 KiB、六个 8 KiB 设备槽位，尾部 4 KiB 预留。
0x10000 的旧参数区保留只读，不自动导入；IAP 日志和镜像区地址不变。

DP 核心测试包含线性/hash 两种模式；tests/dp_storage 使用真实 NVS/Flash 模拟器，
run.py 跨进程验证加载、删点重加、未保存丢失、中断删除及槽位复用。
periodic.conf 验证后台线程，错误注入验证保存失败和保存过程中再次修改。
主机/模拟器不能证明实板断电、Flash 耗时及栈高水位。
