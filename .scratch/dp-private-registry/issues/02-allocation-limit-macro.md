Status: resolved
Blocked by: None

# 分配上限宏

按用户要求在 points.c 顶部定义 DP_MAX_ALLOCATABLE_POINTS，原 SIZE_MAX
反推公式不变，登记检查改为引用该宏。

验证：2026-10-28 native_sim dp_core 1/1（包含超大点数拒绝）通过，格式及 diff 检查通过。
原 ISR 分行改动保持未提交。
