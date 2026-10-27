Status: resolved
Blocked by: None

# 调整周期并整理参数转发联调

验收：实际产品配置为 10000 ms，固件构建通过；同线 RS485 模拟器覆盖北向写等待南向响应及回读；说明包含可使用的地址/站号/示例 RTU 帧，CRC 正确。

## Answer

产品周期已从 500 ms 调为 10000 ms，生成 .config 已核对；离线探测仍为 30000 ms。复用原参数映射并新增 docs/north-south-test.md，包含站号、PDU 地址、模拟从站配置、单字完整 RTU 帧、双字参数测试及同线发送限制。文档中 全部 RTU 帧逐项重算 CRC 通过。

2026-10-27：shared_rs485 9 项通过，包含 test_north_write_waits_for_south_reply_on_same_uart；固件 MCUboot + IAPv3 构建、签名校验及 SGZ 导出通过。日志为工作区 build/slow-collect-shared-tests.log、build/slow-collect-firmware.log。git diff --check 通过，未实板验证。
