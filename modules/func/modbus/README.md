# Modbus support

公共头文件位于 `<modbus_support/>`。本模块不依赖 DP、transmit、vport 或产品点表。

| 接口 | 提供者与职责 |
| --- | --- |
| `modbus_init_server`、`modbus_raw_submit_rx` | Zephyr 官方 RAW 服务端入口 |
| 官方服务端功能码处理 | 01/02/03/04/05/06/15/16 解析、数量校验、位打包及正常/异常响应 |
| `server.h` 的 `mb_server_*` | 本模块的同步适配，整笔访问回调及逐实例上下文 |
| `rtu.h` 的 `mb_rtu_*` | 本模块的完整 RTU 长度/CRC 校验及 ADU 编解码，使用 Zephyr 工具 |
| `rtu_frame.h` 的 `mb_rtu_probe` | 本模块的流检查，根据外部实际时序判定完整帧 |
| `registers.h` | 本模块的公共数值与寄存器字序转换，不认识转发表 |
| `transport.h` | 保留的 Zephyr 传输扩展，不是官方公共接口 |

RAW 输入是完整 ADU，不能直接接收 UART 字节，也不提供 RTU 静默判帧和线 CRC。
这些仍由 APP 时序输入与本模块的 RTU 适配负责，物理 UART 不交给 Zephyr Modbus SERIAL。

## 同步 RAW 服务端

`mb_server_open(unit, &server)` 在启动时占用一个官方 RAW 接口，
`mb_server_process()` 调用官方 modbus_raw_submit_rx，`mb_server_close()` 用于启动失败回退，
或调用方停止并等待全部请求结束后的销毁；不能与新请求提交并发。
配置数量由 MODBUS_NUMOF_RAW_ADU 决定，Zephyr 4.5.0 支持 1–4，当前产品配置 1。
模块默认预算 4，其他 RAW 使用者已占用的接口不会被抢占；不足返回 -ENOSPC。
成功绑定后由 APP 串行调用，递归或同实例并发返回 -EBUSY，关闭正在处理的实例也返回 -EBUSY。

官方用户回调按单寄存器/位提供，且没有 user_data，因此本模块按 RAW 槽位生成独立回调。
第一次官方数据回调时，将已校验请求规范化为 mb_request_t，并调用一次整笔访问后端：

- 读后端填充预清零数据，再由官方逐寄存器/位读取它并构造响应。
- 写后端看到全部载荷，可先预检再执行；后续官方单值回调不再重复执行业务。
- 后端返回 Modbus 异常码，交给官方异常响应函数；不在 transmit 重写响应格式。

每实例固定 250 B 数据暂存，对应标准最大读取 125 个寄存器或 2000 位。
没有请求期动态申请，不使用线程全局上下文；不同实例可以在不同 APP 线程同时执行。
业务后端、输入/输出指针均只在同步调用期间有效，不得保留。

## 固定版本兼容

Zephyr 原生 modbus_raw_submit_rx 默认投递系统 workqueue；4.5.0 新增
CONFIG_MODBUS_WORKQUEUE，可改为官方 Modbus 专用队列。本模块在构建副本中使用受控扩展：
仅本适配器拥有的 RAW 接口在调用线程执行原工作处理函数，未绑定接口保留官方异步投递。
自有 RAW 在复制入站数据前检查调用线程并占用缓冲，防止绕过适配器的提交覆盖活动请求。
这不是官方 RAW 的默认同步能力，不创建额外模块线程。

compat 对四个 Zephyr 4.5.0 源文件校验 SHA256 及替换锚点，只生成构建目录副本，不修改 west 仓库。
相较 4.4.2，RAW 缓冲上限直接复用上游 sizeof(data) 修复，不重复替换。
RAW/serial 的新 modbus_work_submit 入口仍经过同步适配；未绑定自有服务端的请求
回到官方 modbus_work_submit，保留系统/专用队列选择，不强制提交系统队列。
本轮 server_fixes.py 修复高地址在禁用 FP 后的读取循环、响应缓冲边界、非法线圈值、
写数量上限与精确字节数校验，并将错误 PDU 长度统一返回异常 03。
从机 RAW 允许零数据载荷，以便未知功能码或缺失参数交给服务端判断；客户端仍要求至少一个响应数据字节。
只保留接收占用、同步投递、请求接受和异常码翻译内部钩子，移除旧转发框架的整笔替换/完成钩子。

自有适配接口在访问后端前忽略广播读；广播写执行且官方不响应，其他站号也静默消费。
产品范围、放缩及整笔访问规则仍由 transmit 决定；完整 ADU 的 IAP 路由由 transmit
在标准服务端之前分派，不经过这里的标准数量检查。
独立验证见 tests/modbus_support/server.conf：链接官方 RAW 服务端，不启用 DP/transmit。
叠加 workqueue.conf 验证专用队列分支，自有 RAW 的业务回调仍在调用线程同步运行。

## 数据区命名

自有同步适配使用 mb_data_area_t 和 MB_DATA_AREA_* 表示四个 Modbus 数据区，
请求与转发表字段统一命名为 area；它不表示数值类型或 Flash 区域。
Zephyr 原生使用 coil_rd、discrete_input_rd、holding_reg_rd、input_reg_rd 等分区回调，
未提供本模块这一统一数据区枚举；mb_data_area_t 属于本项目的适配接口。
