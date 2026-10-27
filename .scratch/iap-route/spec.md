# IAP 最小路由（延后）

Status: deferred
Blocked by: 下一次调试 IAP 配置时启动
优先级：后续

## 目标

在不恢复 `collect`/`transmit`/`vport` 的前提下，为 `modules/iap`
提供最小的 Modbus/RAW 路由，使 `iap_route()` 真正被调用。

## 背景

2026-10-27 的 DP 最小核心删除了 TR 协议段和旧应用通信装配。`modules/iap`
仍编译并初始化接收器，但当前没有调用者；串口上传不可用。用户明确要求：
升级功能在调试 IAP 配置时再实现。

## 约束

- 不恢复 `modules/collect`、`modules/transmit`、`modules/vport`。
- DP 保持最小核心，不重新引入采集/转发描述符。
- 复用 `iap_route()` 的输入/输出契约和现有 ADU 类型。
- IAP 分区、AES 密钥、镜像上限和 NVS 日志格式保持不变。
- 路由只处理 IAP 地址段：20003、20049–20104、31996–31997；
  必须保留变长 FC16 报文和超过 256 字节的 ADU。

## 方案候选

1. 使用 Zephyr Modbus server/RAW 接口，把完整 ADU 交给 `iap_route()`。
2. 在应用内实现一个极小的 RS485/ADU 分流器，只识别 IAP 功能码和地址段。
3. 若 Zephyr Modbus 的缓冲区/角色约束不合适，再评估独立的最小 RTU 接收器。

选择前先核对当前 Zephyr 4.4.2 的 Modbus RAW 能力、`CONFIG_MODBUS_BUFFER_SIZE`
和 IAP 的 `PACKET_MAX` 约束。

## 验收

- `iap_route()` 在真实串口输入路径上被调用，IAP 地址段可进入接收器。
- 非 IAP 地址明确拒绝或忽略，不影响本地采样、参数和看门狗。
- native_sim 覆盖：单包、缺包、乱序、跨扇区、超大 ADU、错误功能码。
- 默认构建、签名和 SGZ 导出通过。
- 实板上传、时序、总线和掉电结果单独记录。

## Comments

2026-10-27：用户要求“升级在调试 IAP config 的时候来实现”。
本轮只记录任务，不实现路由，也不宣称上传可用。
