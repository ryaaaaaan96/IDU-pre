Status: resolved
Blocked by: None

# 实现每个 LED 状态内部的步骤循环

验收覆盖原有启动/运行/故障、组合图案完整循环、同状态不重启、常灭/常亮保持、队列延迟逐步推进、GPIO 回调期间故障切换及启动/运行错误策略；构建当前固件。

## Answer

已完成 LED 独立指示状态和内部步骤循环，删除 pending_phase/pending_ms/phase_started_ms 等相位交接数据及 LED 自旋锁。status.c 通过自身阶段映射选择状态，播放器只接收原子状态请求，工作队列拥有步骤索引和电平缓存。原图案保持，增加 ATTENTION 组合图案但不绑定业务。

2026-10-27 验证：采集/LED 44 个独立模拟器场景通过（包含新增组合循环、重复状态/保持以及更新后的延迟播放场景）；运行时 17 个场景通过（默认 7、降级 1、调试 8、UART 错配 1）。日志位于工作区 build/led-sequence-acquisition-tests.log 与 build/led-sequence-runtime.log。

MCUboot + IAPv3 构建、签名校验与 SGZ 导出通过，SGZ 152480 字节，镜像摘要 d6e20a1d63db25b68e1b1d6baba955182e0efafd933745fa204d7dd161cd90be。日志 build/led-sequence-firmware.log。git diff --check 通过；未实板验证，未暂存、未提交。

后续范围修正已完成：仅保留初始化、运行、故障三态，移除 OFF/ATTENTION 及其专用测试，保留循环和同状态不重启验证。43 个采集/LED 模拟器场景通过，当前固件构建、签名验证和 SGZ 导出通过；日志为 build/led-three-states-tests.log、build/led-three-states-firmware.log。未实板验证，未暂存、未提交。

按用户要求删除头文件中的 LED 间隔宏，步骤表直接填写 500/100 毫秒，测试使用明确等待时间。采集/LED 43 个模拟器场景通过（build/led-inline-times-tests.log），git diff --check 通过；未提交。
