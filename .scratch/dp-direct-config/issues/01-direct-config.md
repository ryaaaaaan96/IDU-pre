Status: resolved
Blocked by: None

# 配置引用简化

删除 dp_point_cfg_t、dp_source_t、dp_describe_point；读写及参数层直接引用配置行。
登记校验一次；初值在初始化、挂载及恢复默认时展开，存储类型按既有编码转换。
配置行及其引用数据须常驻只读且禁止释放，表描述仍可临时存在。

验证：dp_core、process_runtime native_sim 通过；同一模拟 Flash 的 main/verify
跨进程持久化与恢复默认通过。新增 U8/S16/U32/S32 范围与大于四字节数组初值检查。
既有回调、越界拒绝、失败不覆盖快照测试通过。
默认 MCUboot + IAPv3 构建、验签通过，Flash 90,080 B，RAM 41,576 B。
日志：/tmp/config-dp-{build,test}.log、/tmp/config-persist-{main,verify}.log、
/tmp/config-process-{build,test}.log、/tmp/config-firmware.log。
未进行实板测试。保留用户原有两个未暂存格式修改块。
