Status: resolved
Blocked by: None

# 验证

模块保留普通点快照锁和参数/NVS/状态统一锁，删除独立状态锁。
启动 dp_init/登记独占执行，不加锁；运行接口只在入口加锁，统一出口解锁。
回调保持在锁外，状态查询限线程上下文，可能等待 Flash 操作。
合并 parameter_store.c/h 到 params.c，删除构建项和跨文件后端接口。
存储编码、类型及记录结构私有；删除单调用方缓存提交函数、恒真保存判断和无用历史序号。

DP、process native_sim 通过。双线程并发普通三字节点值读写、持久参数写读、
状态序号单调及状态回调重入访问通过；原登记失败、读保存失败、类型不兼容回退、
main/verify 历史恢复及新增参数测试通过。旧版本生成的 NVS 文件读取通过。
默认 MCUboot + IAPv3 构建及验签通过，Flash 91,044 B、RAM 41,552 B。
未进行实板或完整回归。
日志 /tmp/locks-dp-{build,test}.log、/tmp/locks-process-{build,test}.log、
/tmp/locks-{register-failure,read-failure,main,incompatible,verify,compat}.log、
/tmp/locks-firmware.log。用户原有两个未暂存修改块保持不变。
