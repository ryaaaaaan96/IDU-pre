# 转发表挂载、地址与北向服务

本记录区分“把一个地址空间拆成多张表维护”和“建立多个独立北向服务”。
以下区分 IDU 现状、参考工程证据和待确认方案。当前仅整理设计与术语，未修改运行实现。

## 三类身份与地址

| 概念 | 当前来源 | 含义 |
| --- | --- | --- |
| 逻辑设备 ID | `IDU_DEV_LOCAL`、`IDU_DEV_REMOTE_IO` | DP 中数据归属的身份 |
| 北向 Modbus 站号 | `sample.address` → `tr_server_service()` | 上位主站访问本机的 unit ID |
| 北向寄存器地址 | 转发行 `.reg_addr`，配合 `.reg_type` | 本机对外协议中的位置 |
| 南向 Modbus 站号 | 采集设备的 `.coll_addr` | CO 访问远端设备的 unit ID |
| 南向寄存器地址 | 采集行 `.reg_addr` | 远端设备协议中的位置 |

北向站号当前来自 GPIO 地址输入加偏移，采集周期将其传给服务：
[`idu_inputs_sample()`](../modules/gpio/src/idu_inputs.c)、
[`idu_acquisition_cycle()`](../app/acquisition/acquisition.c)、
[`tr_server_service()`](../modules/transmit/protocols/modbus/server.c)。
挂表设备的 `.dev_id` 或 `.coll_addr` 都不会自动成为北向站号。

[`tables/transmit.c`](../app/protocol/tables/transmit.c) 中的 `.dev_id / .sig_id`
指定数据来源，与表挂在哪个设备上分开。例如北向 FC04/8152 读取本地 DI，
FC04/10000 读取远端设备 10 的 DI；两者可以共享同一信号 ID。
采集表中的 DI 地址仍为 FC04/8152，不会因北向使用 10000 而改变。

## 多张表能否挂在本地设备

可以。每张表使用不同标识登记为 `PRO_MB_SLAVE`，再加入本地设备的绑定列表。
[`dp_table_bind()`](../modules/dataprocess/src/table_registry.c) 不禁止同一设备存在
多个同类型的协议表，但禁止同一设备重复绑定同一表标识。

当前 IDU 每个设备最多 7 个表槽，第一槽被要求是非空处理表；这不是参考项目所有设备的约束。
当前开启 IAPv3 时再使用一个协议段槽，因此本地设备最多容纳 5 张普通转发表；现用 1 张，尚可再增加 4 张。
分文件维护不需要复制测点或另外建立数据池。

## 当前限制：普通转发表尚未按服务隔离

[`reference_transmit()`](../modules/dataprocess/src/reference_config.c) 将所有设备的
所有 `PRO_MB_SLAVE` 表转换为全局映射序列，不保留挂表设备、表 ID、北向站号或端口。
[`tr_read()` / `tr_write()`](../modules/transmit/src/transmit.c) 的端口参数用于回调上下文，
不参与普通映射选择。换一个挂表设备或端口，不能复用同一功能码的同起始地址。

- 同功能码、同起始地址的映射在初始化时拒绝，不能用不同设备 ID 消除冲突。
- 参考配置保留了不同起始地址的覆盖区间重叠兼容；这不是服务隔离。
  涉及重叠区的读取只接受起始地址和长度均精确匹配的 VALUE 行，其他读取可能报错；
  写入没有同样的精确选择规则，不应依赖重叠区组织新协议。
- 普通新表应使用清晰、不重叠的地址段；FC03 与 FC04 是不同的功能码空间。
- `dp_devid_get_prototbl()` 等兼容查询只返回第一个匹配的协议组，不能用它们枚举多表。
- `tr_register_service.device` 目前只选择 `PRO_ADDR_SLAVE` 协议段，
  不把普通 `PRO_MB_SLAVE` 表限制到该设备。

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
当前装配器仍要求它具有 process 表，并将所有普通转发表合并到全局映射。

## 本轮候选方案：两类配置，通过测点身份关联

数据设备配置描述本地或南向数据来源：process 表，以及需要时的 collect 表、南向端口和站号。
虚拟转发设备配置描述北向协议入口：北向端口/站号、转发表和协议段。转发行继续引用已有
`(dev_id, sig_id)`，数据由 DP 保存；南向发送与调度仍归 CO，北向请求与地址空间选择归 TR。

若采用此方案，应用应只配置实际需要的表和端口关系，由模块完成校验、装配与注册；
不要求应用填满七个协议槽，也不以 `devices[0]` 代表转发入口。Iterable Sections 的分散登记继续可用。
这会重新评估 [ADR 0003](adr/0003-iap-protocol-segments.md) 中协议段挂载设备的方式，
尚未形成替代 ADR，不修改现有代码或升级广播策略。

### 第一轮待确认

1. 虚拟转发设备是否只作为协议入口，不配置独立 process 表、不复制测点，状态及命令仍归数据设备？
   推荐如此，符合参考工程虚拟设备无 process 表的配置。
2. 不同转发设备是否需要独立选择转发表、允许复用相同寄存器地址？
   推荐按转发设备隔离读、写、协议段与地址冲突校验；当前一个转发入口同样适用。

上述两项决定后再细化配置结构、逻辑口绑定、兼容接口及迁移验收。
同一物理口多个站号的接收分派与总线仲裁应另行明确；现有代码不自动提供这些能力。
