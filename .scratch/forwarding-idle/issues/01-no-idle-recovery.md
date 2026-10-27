Status: resolved
Blocked by: None

# 删除北向空闲重建

## Answer

根因：tr_modbus_service 把正常无报文时长当作恢复条件，调用 tr_modbus_recover，间接产生周期性 Modbus interface disabled 日志。已删除该条件及独占的私有活动时间戳，保留显式恢复/重配置和实际超时错误处理。

修改前已运行 forwarding::test_idle_service_keeps_interface_ready_without_recovery 并稳定失败：Idle interface must not be reconfigured（after.changing 为 true）。日志 build/forward-idle-red.log；复现命令为 build/IDU-pre/tests/forwarding/zephyr/zephyr.exe -flash_in_ram -test=forwarding::test_idle_service_keeps_interface_ready_without_recovery。

修改后 forwarding 套件 24 项通过，1 项跨进程 reboot probe 按原条件跳过，覆盖空闲后的正常请求、显式恢复回滚、TX 状态异常和处理卡死超时。transmit 套件与 MCUboot + IAPv3 产品构建结果见同名日志；未进行实板验证。日志在工作区 build/forward-idle-*.log。未修改上游依赖，未更改日志级别或 shell 终端配置。

transmit 9 项通过；MCUboot + IAPv3 构建、签名验证及 SGZ 导出通过；git diff --check 通过。首次 transmit 启动误带该配置不支持的 -flash_in_ram 参数，去掉后正常运行通过。
