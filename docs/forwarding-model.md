# 转发表挂载、地址与北向服务

本记录区分“把一个地址空间拆成多张表维护”和“建立多个独立北向服务”。
2026-10-22 已实现数据设备与虚拟转发入口分离；本文说明当前契约及参考工程证据。

## 三类身份与地址

| 概念 | 当前来源 | 含义 |
| --- | --- | --- |
| 逻辑设备 ID | `IDU_DEV_LOCAL`、`IDU_DEV_REMOTE_IO` | DP 中数据归属的身份 |
| 北向 Modbus 站号 | `CONFIG_IDU_FORWARD_UNIT` → `tr_forward_init()` | 上位主站访问本机的 unit ID |
| 北向寄存器地址 | 转发行 `.reg_addr`，配合 `.reg_type` | 本机对外协议中的位置 |
| 南向 Modbus 站号 | 采集设备的 `.coll_addr` | CO 访问远端设备的 unit ID |
| 南向寄存器地址 | 采集行 `.reg_addr` | 远端设备协议中的位置 |

北向站号在初始化由 `.conf` 的 `CONFIG_IDU_FORWARD_UNIT` 确定；
[`tasks.c`](../app/protocol/tasks.c) 将该站号交给 `tr_forward_init()`，之后维护仍使用同一站号。
GPIO 地址输入继续作为本地测点，不触发运行期改号。当前同线南向冲突在开放端口前拒绝。
挂表设备的 `.dev_id` 或 `.coll_addr` 都不会自动成为北向站号。

[`tables/transmit.c`](../app/protocol/tables/transmit.c) 中的 `.dev_id / .sig_id`
指定数据来源，与表挂在哪个设备上分开。例如北向 FC04/8152 读取本地 DI，
FC04/10000 读取远端设备 10 的 DI；两者可以共享同一信号 ID。
采集表中的 DI 地址仍为 FC04/8152，不会因北向使用 10000 而改变。

## 当前装配与入口选择

产品使用两类 const 声明，定义见下文；不再维护七个表槽或 `idu_registers`。
`dp_data_device_bind()` 通过 Iterable Sections 解析 process 与可选 collect，生成
兼容 DP/CO 的小型 `DEV_TABLE_S` 只读关联视图。本地只关联 process，远端 10、11
共享 process/collect 行，并保留独立数据缓存、参数键和采集目标。

`tr_forward_attach()` 解析转发配置，直接关联普通转发表和协议段。
每个 Modbus 接口绑定自己的只读映射视图；读、写、重叠校验均使用该入口。
多个入口可以共用同一张 const 表，也可在同地址访问不同数据。
`forward_id` 与数据设备 ID 分别校验，二者数值相同不形成关联。
同一逻辑口或接口重复绑定不同配置会失败，不覆盖原入口。

- 同入口同功能码、同起始地址的重复映射仍拒绝；保留完整点精确读取的旧重叠兼容。
- 跨空洞、半个多字参数、无效来源及批量校验失败仍拒绝，不产生部分业务写入。
- TR 将地址解析为数据键后交给 DP；process 回调、一次性命令及持久下发逻辑保持。
- 未绑定接口不借用其他入口。无上下文的 `tr_read_register()` 在多个显式绑定存在时
  返回 `-ENOTUNIQ`；实际 Modbus 回调使用当前请求接口。
- 原生/旧参考表消费者未登记任何显式映射时，仍可使用其单一全局映射。
  新产品 DP 配置不含转发表，旧 `dp_params_write_registers(mapping, address, words, count)` 必须显式传入映射视图，不再承担产品地址解析；
  业务使用键式参数接口，协议使用 `tr_write()`。

初始化 `tr_forward_init()` 根据配置的逻辑口找到应用登记的 RAW 接口；
运行期 `tr_forward_service()` 仅维护已初始化服务及恢复。北向站号由应用初始化固定。
当前产品创建一条 CO 线程以及一组 TR 接收/配置队列，线程栈由应用显式配置。

## 参考工程已核实的虚拟转发设备

参考根目录为 `/home/ryan/Documents/demo/MCUdemo/CODE`，只读核查结果：

- `platform/inc/data_process.h:152,205`：虚拟转发设备 `CFG_DEV_VIR=1` 与本地设备 `CFG_DEV_LOCAL=247` 分开。
- `app/cfg/equip/dev_cfg.c:339–360`：本地设备只挂 process 表；虚拟设备挂 Modbus 转发表、
  协议地址段及 UPP 转发表，没有 process 表，也未填写采集端口或采集站号。
- `app/cfg/equip/table/transmit_dcdc1_mb.c:25,498`：同一转发表分别引用本地和 DSP1 的信号。
  行内 `dev_id` 表示数据来源，与挂载转发表的虚拟设备身份分开。
- 公开 TR 配置和控制接口以 `port_id` 为入口，没有发现直接指定 `CFG_DEV_VIR` 的端口注册接口。
  尚不能据此证明原库是否支持多个独立转发实例、如何隔离端口映射；本轮未对这些行为作推断。

之前建议继续把转发表挂在本地设备，是对当时 IDU 限制的折中；用户现在明确要求区分本地数据设备
与转发入口，因此该建议不作为后续目标。只添加一项虚拟 DEV_TABLE_S 也不足够：
旧装配器要求 process 首槽并合并普通转发表；新产品不再使用这条关联路径。

## 已接受方案：两类配置，通过测点身份关联

数据设备配置描述本地或南向数据来源：process 表，以及需要时的 collect 表、南向端口和站号。
虚拟转发设备配置描述北向协议入口：北向逻辑口、转发表和协议段。转发行继续引用已有
`(dev_id, sig_id)`，数据由 DP 保存；南向发送与调度仍归 CO，北向请求与地址空间选择归 TR。

应用只配置实际需要的表和端口关系，由模块完成校验、装配与注册；
不要求应用填满七个协议槽，也不以 `devices[0]` 代表转发入口。Iterable Sections 的分散登记继续可用。
[ADR 0006](adr/0006-separate-forwarding-devices.md) 已接受替代 ADR 0003 的数据设备挂载选择；
协议处理及广播行为保持。

### 已接受的结构示例

对应公共接口为 `dataprocess/table_registry.h` 和 `transmit/forward.h`。
`data_device_cfg` 属于配置装配接口：按设备建立 DP 数据视图，以及可选的 CO 采集视图；
`tr_forward_cfg` 由 TR 描述北向入口。执行模块各自取得所属视图，表内容继续通过 Iterable Sections 登记。

```c
struct data_device_cfg {
    uint8_t dev_id;                /* 数据归属：本地或南向设备。 */
    uint16_t process_table;        /* 必填，处理表标识。 */
    uint16_t collect_table;        /* 0：不启用南向采集/下发。 */
    uint8_t collect_port;          /* CO 逻辑总线号，不是 UART 编号。 */
    uint8_t collect_addr;          /* 当前 Modbus 南向站号。 */
};

struct tr_forward_cfg {
    uint16_t forward_id;           /* TR 内部身份，与 DP dev_id 分开。 */
    uint16_t port_id;              /* 北向逻辑端口。 */
    uint16_t transmit_table;       /* 本入口独立使用的转发表标识。 */
    uint16_t protocol_segments;    /* 0：没有协议地址段。 */
    bool route_broadcast;          /* 是否将广播交给协议段。 */
};

static const struct data_device_cfg data_devices[] = {
    {
        .dev_id = IDU_DEV_LOCAL,
        .process_table = IDU_TABLE_LOCAL_PROCESS,
        .collect_table = 0,
        .collect_port = 0,
        .collect_addr = 0,
    },
    {
        .dev_id = 10,
        .process_table = IDU_TABLE_REMOTE_PROCESS,
        .collect_table = IDU_TABLE_COLLECT,
        .collect_port = 0,
        .collect_addr = 1,
    },
    {
        .dev_id = 11,
        .process_table = IDU_TABLE_REMOTE_PROCESS,
        .collect_table = IDU_TABLE_COLLECT,
        .collect_port = 0,
        .collect_addr = 2,
    },
};

/* 当前示例启用 IAPv3；未启用时协议段标识为 0、广播策略为 false。 */
static const struct tr_forward_cfg forwarding_devices[] = {
    {
        .forward_id = 1,
        .port_id = IDU_PORT_FORWARD,
        .transmit_table = IDU_TABLE_TRANSMIT,
        .protocol_segments = IDU_TABLE_PROTOCOL_SEGMENTS,
        .route_broadcast = true,
    },
};
```

`collect_table=0` 时不解释该行的 collect_port/collect_addr，也不生成采集目标；
本地采样仍由现有输入模块和业务映射写入 DP。两台远端共享 const 点表，仅各自保存运行状态。
`forward_id` 不作为数据键调用 dp_set；转发表中的 dev_id 仍指向已有数据设备。
例如北向 10000 → `(10, IDU_SIG_DI)`，10001 → `(10, IDU_SIG_CURRENT_1)`，
8152 → `(IDU_DEV_LOCAL, IDU_SIG_DI)`。北向数据不额外复制一份。

北向站号由应用初始化配置传给 TR，不随后续拨码采样改变；配置结构仍不重复保存 unit_id。
采集总线与北向逻辑口仍由应用 ports 编排到当前同一 RS485；配置分开不增加物理多主仲裁能力。
示例仅描述当前 Modbus/RS485；未来 CAN 标识及分帧规则不在本轮提前加入字段。

该结构将普通转发表与协议段直接归虚拟转发设备，原 idu_registers 对象及额外 protocol 指针
已由模块内部装配；应用无需同时维护设备挂表和另一份转发服务关联。
运行时按接口定位该转发身份的映射，读、写及冲突校验不再依赖全局合并。

### 确认与实施状态

2026-10-22，用户认可上述结构并调用 to-spec：虚拟转发设备不配置独立 process 表，
数据仍归原数据设备；各转发设备独立选择转发表，并在读、写、协议段和冲突校验中保持隔离。

- [正式规格](../.scratch/forwarding-devices/spec.md) 包含用户故事、实施决定、测试与排除项。
- [实施任务](../.scratch/forwarding-devices/issues/01-separate-data-and-forwarding.md) 已完成；[验证记录](../.scratch/forwarding-devices/validation.md)记录构建、测试及资源变化。
- 实施结果与验证记录见关联任务；实板时序、运行时栈余量和 NOR 掉电仍需实测。

同一物理口多站号接收分派不纳入本轮；继续沿用每个北向逻辑口一个活动入口及既有同线事务仲裁。
