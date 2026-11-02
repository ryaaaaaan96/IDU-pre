Status: resolved
Blocked by: 03-stream-forwarding
Type: task

# 复用 Zephyr RAW 服务端并收敛 Modbus 职责

## 目标与约束

用户要求找出当前可复用的 Zephyr Modbus 能力并执行修复。
保留 APP 同步线程、逐点快照、整笔映射预检和非事务写入语义。
本轮仍遵守用户“不提交”要求：不暂存、不提交、不推送，保留先前所有修改。

## 实现

- 实际使用 modbus_init_server(MODBUS_MODE_RAW) 与 modbus_raw_submit_rx。
- 标准 01/02/03/04/05/06/15/16 的解析、数量检查、位打包和正常/异常响应委托官方服务端。
- modules/modbus 新增 server.h/server.c：第一次官方数据回调进入一次整笔后端，
  后续单值回调使用同一请求缓存；按 RAW 槽位生成独立回调，不使用全局当前请求。
- 只有适配器拥有的 RAW 接口通过构建期钩子同步调用原工作处理函数；其他接口保持官方异步。
  请求所有者用原子线程指针表示，递归/并发拒绝；接收占用在复制前检查，阻止外部提交覆盖活动请求。
- 标准寄存器编解码与地址空间/字序类型移入 modbus 模块的 registers.h、server.h 公共接口。
  transmit 只保留地址映射、权限/完整测点预检、放缩、DP 访问和协议适配入口。
- 删除旧的整笔替换与请求完成钩子；异常码由官方异常构造函数生成。
- RAW 接口上限依 Zephyr 4.4.2 为 4，当前产品配置 1；模块每实例暂存固定 250 B，
  避免 IAP 的 MODBUS_BUFFER_SIZE=1100 放大不使用的槽位和后端暂存。
- RAW 不提供 RTU 字节判帧或线 CRC，保留现有时序输入和无分配 RTU 适配。
- IAP 路由、南向采集和透传未在本轮启用。

## 固定版本修复

只在构建目录生成受 SHA256 和锚点校验的 Zephyr 4.4.2 副本，west 依赖源码不修改。
server_fixes.py 修复禁用 FP 后的高地址读取循环、读响应边界、非法线圈值、
多写数量及精确字节数检查，并将错误请求长度返回异常 03。
仅服务端允许零数据载荷以判断未知功能/缺参；客户端仍保留至少 1 字节响应检查。

## 验证

2026-10-31：

- transmit_stream：16 项通过，含 1000 批噪声、完整映射/放缩、发送失败不重放，
  以及两个 APP 线程并行访问独立设备。阻塞第一实例回调不阻塞第二实例。
- 同套件 ASan/UBSan 通过，ASAN_OPTIONS=detect_leaks=0。
- modbus_support/server.conf：3 项 RAW 专项和原 3 项编解码通过，DATAPROCESS=n、TRANSMIT=n。
  验证最大寄存器/位读取、高地址、错误长度/线圈值/字节数/数量、地址溢出、
  调用线程、每请求单次后端、递归/外部覆盖保护、接口回收、普通 RAW 默认 workqueue 和客户端异常。
- protocol_port：模拟 UART/GPIO 上的产品 APP 线程读写及方向控制通过。
- 普通 modbus_support、dp_core、process_runtime 和构建配置检查通过。
- 默认 MCUboot/IAPv3 固件构建与 imgtool 验签通过。日志在 /tmp/raw-*-build.log、
  /tmp/raw-*-test.log、/tmp/raw-firmware-build.log。
- 格式、Python 静态检查、文档链接与 git diff --check 通过。

## 验证限制

未烧录、未进行 RS485 实板时序、持续输入、栈高水位或 CAN 验证。
生成 SGZ 只证明构建与打包，不表示串口 IAPv3 已可用。

最终默认应用 Flash 103,692 B、RAM 65,464 B，MCUboot 不变。
资源比较及 RAW 数量影响见 docs/memory-management.md，所有修改保持未暂存、未提交。
