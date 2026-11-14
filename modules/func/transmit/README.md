# transmit

对外从机协议模块，公开头文件 `<transmit/transmit.h>`。
APP 的 protocol 创建线程并经 vport 交付字节和事件；模块不创建线程、定时器或工作队列。
当前内置 Modbus RTU，也提供一个 CUSTOM 协议处理项供应用现有协议接入；
没有动态协议注册中心，新增内置协议时修改本模块的分派代码。

## 公开头文件

应用统一包含 `<transmit/transmit.h>`，该入口按依赖顺序包含以下声明：

- `modbus.h`：Modbus 转发表及从机配置。
- `protocol.h`：报文视图、匹配结果、协议回调与协议配置。

`transmit.h` 提供不透明 `tr_engine_t` / `tr_session_t` 句柄及创建、销毁接口。
各子头文件自行包含所需依赖，不要求调用方按特定顺序包含；内部函数放在私有头文件。

## 共享服务与通道会话

一个 engine 引用一个 vport、共享协议配置和转发表，每类协议最多一个。
Modbus engine 占用一个 Zephyr RAW 接口和一个从站地址，N 个 session 共用该接口。
不同从站服务可创建多个 engine，RAW 接口数量仍受 MODBUS_NUMOF_RAW_ADU 限制。
同 engine 必须接受相同站号、协议集合、点表、RTU gap、整帧超时和发送超时。
波特率归 APP 硬件配置，engine 无法校验；APP 必须检查各通道所需 gap 是否一致。
不同波特率在时序兼容时可以共享，不兼容时创建独立 engine 并增加 RAW 接口数。
产品装配 RS485-1/2 两个 session，共用一个 engine，各有一个协议线程。

每个 session 对应一个非零 channel_id，拥有独立 RX、逐字节时间戳、TX、
协议选择、接收时间、半帧、错误和恢复状态。channel_id 只用于路由和原路回复；
DP device_id/signal_index 仍来自共享转发表，不建立通道到逻辑设备的映射。

## 创建与生命周期

- INIT 时 `tr_engine_create(config, &engine)` 校验协议并打开共享 server。
- APP 挂载通道后调用 `tr_session_create(engine, config, &session)`，内部申请缓存并绑定接收者。
- 配置结构按值复制；协议数组、点表、地址段、放缩和回调配置必须常驻且运行期只读。
- 所有 session 创建成功后，由 APP `vport_start()`，再开放接收并启动线程。
- 创建失败时 out 为 NULL，本次资源回收且不遗留绑定；已有 session 不受影响。
- INIT 回滚或 STOPPED 清理时 `tr_session_destroy()` 先解绑再释放；
  `tr_engine_destroy()` 仅在全部 session 销毁后关闭 server，否则返回 -EBUSY。
- 生命周期操作由 APP 串行管理；运行后必须先停止并等待全部收发、业务调用结束，再 stop 和销毁。
  destroy 不代替线程退出等待，不支持 READY 下热删除。

engine 启动期单独申请服务状态。每 session 一次申请：会话头 + rx_capacity 个
uint64_t 时间戳 + RX 字节 + TX 字节，运行期不分配或扩容。配置容量须计入系统堆预算。

## 并发与回调

不同 session 可由不同 APP 线程并发调用；同一 session 的重复或递归输入返回 -EBUSY，
APP 必须保证同一通道事件顺序。模块不创建线程、定时器或工作队列。

组帧及协议 check 按 session 独立推进。engine 互斥锁覆盖完整请求执行、DP/扩展路由/
CUSTOM execute 和响应编码。响应完整写入本 session TX 后释放锁，再同步从来源通道发送。
因此发送阻塞不占共享执行锁；慢业务回调仍会阻塞其他 session 的请求执行。
共享服务回调内递归进入同 engine 的任一 session 返回 -EBUSY，不使用递归锁覆盖请求状态。
CUSTOM check 也登记当前调用线程，递归输入同 engine 返回 -EBUSY；不同线程的 check 仍可并发。
执行锁使用 K_FOREVER；协议整帧超时不限制执行锁等待，慢 DP/Flash/IAP 回调会增加其他通道延迟。
APP 须给执行回调设定耗时预算；不得在持锁回调中等待另一个同 engine 请求完成。
发送失败消费请求，不自动重放；不同端口发送能否并行取决于 APP 线程调度。

CUSTOM 的 shared context 是 const 配置指针；check/execute 同时接收 session 的
custom_context。check 只检查、不产生业务副作用，可在不同 session 并发执行；
所有可变协议状态放在本 session context，execute 在 engine 锁内调用。

## 容量与扩展路由权限

session 配置 channel_id、rx_capacity、tx_capacity、max_frame、allow_routes 和 custom_context。
要求非零容量、max_frame <= rx_capacity；Modbus TX 至少 256 B，max_frame 至少 4 B。
协议检查和等待使用 min(protocol.max_frame, session.max_frame)，容量溢出只损坏本 session。
允许扩展路由时，session.max_frame 必须覆盖该 Modbus 协议配置上限；
禁止路由的普通通道可以使用 256 B 缓存，共享另一通道的 1100 B IAP 协议配置。

allow_routes=false 时，命中的扩展地址不调用回调、不回退普通测点，单播返回异常 02，
广播静默且不执行动作。允许时回调获得 channel_id；升级归属由 IAP/产品适配层跨请求维护，
不能长期持有 engine 锁。当前产品只允许 RS485-2 进入升级入口。

## 接收与选择

- 首次锁定登记的第一个协议，等待时保持锁定；完整有效后先执行、再消费，继续锁定。
- 明确不匹配才环形切换，保留同一份未消费数据和接收时序；业务错误不切换。
- 协议只检查，不删除缓存或产生业务副作用；同一视图可重复检查，不使用隐含字节游标或跨候选解析状态。
  返回完整结果时必须给出有效消费长度。
- 每轮每协议检查一次，全部失败后丢一个前缀字节。不能把截断后的 RTU 残片视为合法起点。
- 期限按候选首字节实际接收时刻计算，不因输入或切换重新计时；空缓存不计时。
- APP 在无数据时仍提交 POLL；IDLE 与 POLL 都可推进判帧，不能越过接收队列中较早的字节。
- 接收损坏或容量溢出清理候选并回到首协议，后续先等待有效间隔再恢复。
- 时间戳必须单调；倒退返回 -EINVAL。一次调用的恢复工作量由固定接收容量和协议数量限制。

RTU 通过 modbus_support 的 `mb_rtu_probe()` 检查，结合实际字节间隔判断起止边界及帧内中断。
当前同实例 CUSTOM 视图带有 RTU 推导的边界信息，内容判帧的 CUSTOM 可忽略该信息；
未来其他时序协议需要扩展各自的时序判定，不能将 RTU 的静默参数直接作为其协议规则。
仅 CUSTOM 的实例采用接收时间变化作为恢复边界；其完整性应由自身帧头/长度/校验决定。

## Modbus 点表

点表行使用 tr_modbus_point_t，通过 area、address、count、word_order 配置
Modbus 对象类别、零基地址、地址数量与字序；每行内嵌 dp_index_access_t access，
包含 device_id、signal_index、value_type、flags，scale 指针单独放在本行。
不再提供 tr_mapping_t 或 mapping.h，不增加第二张查找表或动态分配。
APP 通过 engine/session 创建接口装配，不直接调用 Modbus 服务端。
线圈和保持寄存器读写，离散输入和输入寄存器只读。
access.flags 配置 GET/SET 回调、读结果写回及提交前 CHANGE 处理，DP 负责执行；
transmit 只使用 dp_get_value_by_index/dp_set_value_by_index，不查询 key 或点表回调开关。
输入寄存器和离散输入为只读区域，登记时拒绝其 SET/CHANGE 标志、任何未知标志，
以及未同时配置 GET_CALLBACK 的 READ_WRITEBACK。
功能码决定访问对象类别，DP 校验与回调仍可拒绝写入。
寄存器数值的 count 必须为 U8/U16/S16 的 1 或 U32/S32 的 2；
WORDS/STRING/BYTES 的 count 表示 16 位寄存器数，最多 VALUE_WORDS。
WORDS 使用本地 words 成员，STRING/BYTES 使用原始 bytes，每寄存器两字节，不补字符串终止符。
高字在前为默认，低字在前反转寄存器顺序，不改变寄存器内大端编码。
位点行映射整数低 count 位，不超过该类型位宽；读取允许截取，写入必须覆盖该行全部位。
注册时校验地址重叠、长度、类型、放缩、缓冲区、access.flags 及只读区域写事件；
DP 目标是否可访问在实际读写时判断，不新增 DP 元信息查询，不在登记时执行业务读回调。

标准功能码的分派、协议数量/字节数校验和响应构造由 Zephyr RAW 服务端提供，
transmit/src/modbus.c 负责地址查找、整笔预检及寄存器编码，
transmit/src/mapping.c 统一处理 DP 读写、正向/逆向放缩与对外数值转换。
内部读写接口不对应用公开；写入预检不访问 DP，实际提交才执行业务回调。
公共寄存器编解码、地址空间及字序类型位于 modbus_support/registers.h 与 server.h。
官方第一次数据回调进入一次整笔后端，后续逐寄存器回调使用该请求缓存，不重复读取或写入 DP。
通过构建期同步适配，业务仍在 APP 调用线程运行，未转移到系统工作队列。

支持 01/02/03/04/05/06/15/16；未知功能码返回异常 01。
地址空洞、对象类别不匹配或部分点写返回异常 02；参数/转换/范围及 DP 返回的 -EINVAL 统一返回 03，
其他 DP 执行错误返回 04。
请求先做整体协议预检；多点写按地址顺序执行，首错停止、不回滚。
读请求逐点快照，每个涉及行只读一次，不保证跨点同时更新。
其他站号静默消费并保持锁定；广播仅执行写且不响应，不做跨实例去重。

## 放缩与类型转换

通用 `<common/scale.h>` 的 `value_scale_t` 保存分子、分母、偏移，转发行使用 const 指针。
scale 为 NULL 就跳过计算，不额外配置 flag；同一只读放缩对象可跨模块复用。
无放缩时 DP 直接按 access.value_type 读写，类型转换统一由 DP 负责。

正向：对外 = 按 access.value_type 转换后的 DP 值 × numerator / denominator + offset；写入做逆换算。
分子分母非零，除法向零舍入，int64_t 中间运算溢出返回错误。
读取：DP 原值先转换为 access.value_type，再正向放缩，结果再以 access.value_type 保存并编码。
放缩前可能已丢失高位或改变符号，这是约定行为。
例如 DP U32 值 100000，对外 U16，除以 100：先转成 34464，再得到 344。
写入：解码得到 access.value_type 的值，逆放缩后仍按 access.value_type 表示，再交给 DP 转为内部类型。
例如分子 100、分母 1：对外 U32 70000 写入内部 U16 得到 700。
不配置独立计算类型；中间运算始终使用 int64_t，逆放缩结果超出对外类型时保留低位。
最终整数位宽转换保留低位，不饱和；业务回调仅在 DP 内执行。

发送失败也消费已选择请求，不自动重放或重发，不解除锁定；主站重试是新请求。
模块原理见[整体设计](../../../docs/transmit-design.md)，产品表见
[产品协议](../../../product/app/protocol/README.md)。模拟器证据不能替代实板时序验证。

## 完整 ADU 地址段

tr_modbus_t 可同时配置 points 和 routes，也可只配置 routes。
tr_modbus_route_t 配置 address、count、callback 和 context；范围仅匹配请求的起始地址，
不是请求寄存器数量的上限。登记时拒绝路由互相重叠，以及路由与任意数据区测点的地址重叠。
路由保留所有功能码的该起始地址空间，不配置 area、数值类型、放缩或字序。

处理顺序为 RTU 边界及 CRC 校验、站号过滤、地址段回调、标准 RAW 测点处理。
回调接收来源 channel_id 和完整 modbus_adu，自行校验功能码、数量、载荷及会话；已匹配后不回退测点。
返回 0 编码回复，TR_MODBUS_ROUTE_SILENT 静默消费，其他错误返回调用方且不自动响应。
广播可进入回调，但无论回调返回什么都不发送响应；回调须自行决定是否接受广播动作。
回调在 APP 线程同步执行且每请求一次，Flash 长操作应由后端线程完成。

这种回调用于旧 IAPv3 等扩展协议，区别于经过标准功能码校验的普通寄存器访问。
不把 RAW 加入 common/value_type_t，也不把完整升级数据塞入 DP 点。
只有配置 routes 才允许 max_frame 超过 256，最大为 CONFIG_MODBUS_BUFFER_SIZE；
标准点表仍由官方服务端限制合法数量。缓存和堆预算由 APP 同步扩大。

## 可选接收过滤

session 配置 accept/accept_context，可在进入组帧前接受或丢弃事件；NULL 接受全部。
过滤在同会话 busy 保护内同步执行，false 表示已消费但不改变会话状态，返回成功。
回调须短小、不得递归输入或销毁资源；上下文须覆盖会话生命周期。
产品 protocol 使用此入口检查 STATUS_RUN，模块本身不依赖产品状态。
