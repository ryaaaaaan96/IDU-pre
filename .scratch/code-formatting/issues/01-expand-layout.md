Status: resolved
Blocked by: None

# 展开单行逻辑并统一行宽

验收：自有 C/H 和 Python 无超 100 列行；格式检查通过；C 词法 token
（去除注释/空白、合并相邻字符串）及 Python AST 前后相同；执行完整回归和固件构建。

初始用户 points.c ISR 分行改动保留为未提交修改，未纳入本轮提交。

## 验证（2026-10-28）

- clang-format 23.1.3 覆盖 52 个 C/H，ruff 0.14.2 覆盖 15 个 Python。
  全部文件不超过 100 列；clang-format --dry-run --Werror、ruff format --check 通过。
- 格式前后 C token 对比相同（忽略注释/空白、归并相邻字符串），Python AST 相同。
- tests/run.sh 中 DP、参数跨进程、产品装配、ADC、Modbus、IO 10 场景、
  watchdog 25 场景、IAP、镜像和构建配置测试通过。可选旧压缩器互操作项按环境跳过。
- tests/run.sh 最后 debug_session 因沙箱 socket 禁止而报 PermissionError；
  同一代码在允许本机回环套接字的环境单独运行，4/4 通过。未改测试断言或规避测试。
- 默认 MCUboot + IAPv3 构建、验签、产物导出通过。
  应用镜像 SHA256 摘要与格式前相同：
  72cd35cfba7c62dd9363d02dcaf9a2de10583941197e3432de13f6455cd880d8。
- git diff --check 通过。初始用户 ISR 分行保留未提交，清除其行尾空格。

只做排版和格式配置，不改变逻辑、包含顺序或运行时字符串。未上板。
