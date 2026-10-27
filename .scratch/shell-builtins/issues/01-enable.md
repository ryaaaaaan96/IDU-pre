Status: resolved
Blocked by: None

# 配置官方 Shell 命令

验收：实际固件配置启用 SHELL_CMDS、SHELL_VT100_COMMANDS、KERNEL_SHELL 和 REBOOT；固件构建、签名和导出通过。实板终端行为另行验证。

## Answer

已启用 SHELL_CMDS 和 SHELL_VT100_COMMANDS，保留已有 KERNEL_SHELL。实际 .config 核实四项配置均为 y，MCUboot + IAPv3 构建、签名校验与 SGZ 导出通过（工作区 build/shell-builtins-build.log）；git diff --check 通过。未执行实板重启或终端清屏测试。README 已说明 clear 和 kernel reboot cold 的作用。
