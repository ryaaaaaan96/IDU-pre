Status: resolved
Blocked by: None

# 标志位使用移位表达式

按用户要求将 SIG_FLAG_DEF 的数值写成 `(1U << n)`，每项单独一行。
保留原位号 0、3、5、6、7，数值和点表行为不变。
登记接口本轮只说明分工，暂不合并。

验证：2026-10-28 native_sim dp_core 1/1 通过，git diff --check 通过。
