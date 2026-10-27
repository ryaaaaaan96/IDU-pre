# 启用官方清屏和重启命令

通过产品 prj.conf 启用 SHELL_CMDS 与 SHELL_VT100_COMMANDS，复用已启用的 KERNEL_SHELL 和 REBOOT，提供 clear 与 kernel reboot cold。仅修改配置与使用文档，不增加自定义 reboot 别名。清屏不清故障记录，官方人工重启不经终止故障上报。
