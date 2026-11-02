Status: resolved
Blocked by: 02-typed-dp
Type: task

# 实现 vport、流式转发与 APP 串口接入

## 范围

用户要求继续实施，并确认有放缩的行配置 S32/U32 计算类型。
用户随后要求通用放缩类型归 common，转发行用可空指针跳过无放缩路径。
用户明确要求本轮不提交：修改仅保留工作区，不暂存、不创建提交、不推送。

## 实现

- 新增 vport：APP 持有端口对象，线程同步交付 DATA/IDLE/POLL/ERROR，发送回调带超时。
- 新增 transmit：一次分配收发缓存与每字节时刻，首协议锁定、跨帧保持、解析失败环形切换。
  期限不重启；共同起点恢复；损坏/溢出清理候选并等待有效起点。
- Modbus 公共模块新增纯检查 mb_rtu_probe；仅使用 SERIAL/RAW 传输时加载历史兼容扩展。
- 独立转发表按 device_id + process 枚举定位 DP；实现八个标准功能码、权限、字序、数组及位点，
  部分读/完整写、空洞预检、非事务写失败、其他站号及广播静默消费。
- common/scale.h 定义 value_scale_t，提供通用整数正逆换算及溢出检查。
  转发行 const 指针为 NULL 时不计算，无额外 flag；业务回调仅在 DP 执行。
- APP protocol/port.c 持有 ISR 队列、微秒时基、线程、UART 中断发送和 RS485 DE 控制。
  使用应用自有 idu,forward-port 绑定，避免与 Zephyr Modbus 串口服务端争用 UART。
- 产品启动先登记 DP，再读保存站号并绑定转发实例。保存站号下次启动生效；
  当前地址范围从历史拨码 1–16 改为与保存站号一致的 1–246，避免重启时地址 17 以上初始化失败。
- 产品全量本地点映射在 tables/forward.c；设备 ID 改为枚举。
- 没有恢复南向 collect、透传或 IAP 路由。CAN 接入由 APP 后续实现，模块不感知 CAN ID。

## 验证

2026-10-31，主机/native_sim：

- transmit_stream 15 项：逐字节与多帧、锁定切换、原始边界保留、超期不重计时、
  RTU 帧内间隔、噪声恢复、溢出、站号隔离、广播、字序、数组、位点、权限及地址空洞、
  部分写拒绝、DP 非事务失败、正负整数转换、先放缩后收窄、发送失败不重放、
  登记分配失败及运行期禁止分配。
- 同套件 ASan/UBSan 通过，含 1000 批确定性噪声输入；ASAN_OPTIONS=detect_leaks=0。
  成功实例按契约保持到重启，无运行期注销。
- protocol_port 使用 UART/GPIO 模拟器执行真实 APP 线程，验证读 DI、写采样周期、
  方向引脚回落及故障锁定后停止提交业务。
- modbus_support、dp_core、process_runtime、adc 相关回归和 Python 构建配置检查通过。
- 默认 MCUboot/IAPv3 固件构建与 imgtool 验签通过；生成产物在工作区 build/hex 下。
- 格式、文档本地链接与 git diff --check 检查通过。

日志：/tmp/tr-*-build.log、/tmp/tr-*-test.log、/tmp/protocol-port-test.log、
/tmp/tr-sanitized-test.log、/tmp/transmit-firmware-build.log。

## 未执行

没有烧录，没有验证实板 RS485 电气、DE 时序、满速接收、长期栈/堆峰值或 CAN。
IAPv3 路由尚未装配，生成 SGZ 不代表串口上传可用。

用户要求判帧文件使用明确名称，stream.h/stream.c 改为 rtu_frame.h/rtu_frame.c，函数名和行为不变。

用户要求通用整数辅助函数不留在 Modbus 实现中：移至 common/src/value.c，由 common/value.h 声明，
统一命名 value_integer_bits/get/from_bits；DP 转换复用同一实现，删除重复 switch。
保持未提交；复验 DP 普通/hash、转发与模拟端口回归。

整数辅助函数迁移后，DP 普通/hash、15 项转发及模拟端口测试通过；默认固件构建与验签通过。
日志 /tmp/common-value-*-test.log 和 /tmp/common-value-firmware.log，仍未提交。
