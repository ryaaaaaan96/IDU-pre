# ADR 0014：DP 最小原子核心

状态：已接受（2026-10-27）

## 背景

产品只运行本地 process：采集通过同步 `dp_set_sig_value()` 写入，转发、异步样本、
参数下游读回和参考表目录都不参与运行。原 DP 模块仍有五套初始化入口、
异步队列/线程、定时器、参考表和 collect/transmit 描述符，源码约 5,800 行，
链接文本约 18 KB，堆预算 20.7 KB。

## 决定

把 `modules/dataprocess` 收缩为最小原子核心：

1. 唯一入口 `dp_register_model(model, params_area)`：业务层逐组登记；每组校验后
   按实际字数申请自己的点值块。参数区在首次访问参数接口时挂载。
2. 只保留单点同步写读：`dp_set_sig_value()`、`dp_get_sig_value()`、`dp_read()`。
3. 点表行只解释常量、本地点、持久参数、范围、读回调和命令回调。
   命令点执行 `handler`，不保存触发值；状态点执行 `read`。
4. 参数只做本机 NVS：保留 `parameter_store.c` 的记录/提交镜像格式，
   删除下游转发、待确认意图、读回恢复和去抖业务线程。
5. 删除异步样本队列、业务线程、定时器、参考表目录、`data_process.h` 字节适配层、
   collect/transmit 描述符及相关测试。
6. 删除不再可用的 `modules/collect`、`modules/transmit`、`modules/vport`
   和旧应用通信装配。

## 结果

- DP 源码从约 5,800 行降到约 1,300 行；模块只剩 `points.c`、`params.c`、
  `parameter_store.c` 和 5 个头文件。
- 产品应用 Flash 从 106,100 B 降到 88,852 B，RAM 从 78,676 B 降到 40,728 B；
  系统堆从 45,404 B 降到 16,988 B。
- 点表、参数键和 NVS 记录格式保持不变；已有参数记录可继续读取。
- 设置项改为本机同步持久化，掉电恢复不再依赖南向读回。

## 后果与未决

- 采集/转发和 IAP 路由不再是本产品能力。`modules/iap` 仍保留 `iap_route()`，
  但当前没有调用者；重新启用通信需要新增独立的 Modbus/RAW 路由适配层。
  IAP 最小路由计划在下次调试 IAP 配置时实现，见
  [.scratch/iap-route](../../.scratch/iap-route/spec.md)。
- 参数宽度/类型变化会按格式不兼容清空 `idu-settings`，与旧行为一致。
- 实板参数持久化、掉电和 IAP 上传需单独验证；本轮只有主机/native_sim 证据。
- 历史 ADR 0006、0008、0010 记录当时方案，不代表当前能力。

## 验证

见 [验证记录](../../.scratch/dp-minimal-core/validation.md) 和
[测试说明](../docs/testing.md)。

## 2026-10-28：收敛点表接口

替代上述模型登记及并行读接口：使用 `dp_register(params_area)` 一次登记分区，
`dp_register_table(table)` 逐张登记点表；读写统一为 `dp_set_para()` / `dp_get_para()`。
`struct dp_model_cfg` 改为 `struct dp_table`。删除兼容头 model.h、公共 dp_read、
dp_stop/dp_stopped 及 DP 故障停止状态。status 锁存整机阶段，主循环停止业务；
DP 不参与整机故障策略。

保留参数恢复默认和状态查询；删除无实际刷新/重试行为的 flush/recover 及空的取消复位。
IAP 直接查询存储是否就绪。同步写已经落盘，无需第二次 flush。
删除原刷新/恢复事务命令点（31092、31093），访问时返回点不存在，不复用这些 ID。
NVS 记录类型编码、逻辑键和镜像格式不变。

## 2026-10-28：设备树直接绑定参数分区

删除 dp_register() 和运行时分区登记状态。DP 强制要求 `dp-params` 设备树别名，
指向启用的固定分区；缺失时编译失败。应用只调用 dp_register_table() 登记点表。
板级节点及 label 从 idu_settings / idu-settings 改为 dp_params / dp-params；
保留外部 Flash 偏移 0x10000、大小 0x4000 及 NVS 内容格式，改名不擦除存储。

## 2026-10-28：每张表持有点状态

以降低实现复杂度为优先，替代全局 slots/states 和点值堆分配：只读行定义仍由产品
提供，表描述改为可写并持有等长的独立 dp_point_state 数组，DP 挂接单向链表。
登记接口仍是 dp_register_table(table)，参数去掉 const；读写接口、回调、长度和
范围校验、本机参数持久化及 NVS 格式不变。登记须在线程启动阶段串行完成。

删除最大表数、最大点数、总字数预算和点值堆配置；参数数量和提交预算保留。
参数层枚举表链表并保存直接行引用，不再保存全局点下标。
每点预留完整值数组，使静态 RAM 略增，但去掉偏移计算、分配失败和跨表下标转换。
默认固件链接结果见内存管理；业务故障状态仍由 status 维护。

## 2026-10-28：只读表、私有设备目录与秒级更新时间

在用户确认后替代上一节的应用状态数组：公共 dp_table_t 只含只读行及数量，
设备 ID 由 dp_register_table(device_id, table) 入参指定。私有 dp_registered_table_t
展开保存描述字段，用柔性数组与运行数据一次分配；公共类型不暴露 next 或 data。
同表可用于多个设备。失败不发布节点，分配失败可重试。

DM_SIG_CFG_S 改为 dp_point_def_t，所有 DP 类型使用 dp_*_t 别名；字段统一
signal_id/size/flags/type/write/read。读回调只生成 dp_value_t，模块记录快照时间。
数值改为联合体，运行数据只保留 value 和 uint32_t updated_at_s。更新时间为 UNIX 秒，
默认、历史恢复及未校时记 0；不将 uptime 当作日期，超出 32 位范围记未知。
不引入 DP 授时接口，由系统 sys_clock_settime 设置墙钟，零墙钟偏移视为未校时。

参数共享设备点数据，删除第二份参数点缓存。参数 NVS 编码、哈希键和版本不变；
UNIX 时间不持久化。分配预算、结果及验证见内存管理和 .scratch/dp-private-registry/。

## 2026-10-28：明确业务回调名称

点定义和内部配置的 write/read 字段改为 write_callback/read_callback，类型改为
dp_write_callback_t/dp_read_callback_t。明确它们是业务回调，与 DP 自身的读写接口区分；
函数参数、返回值、执行顺序及保存行为不变。

点表字面值及内部解析配置中的 initial 同时改为 default_value，明确它是产品默认值，
不仅用于启动初始化，也用于恢复默认；只改名称，不改变数值或存储格式。
