Status: resolved
Blocked by: None

# 验证

重复键检查改为 Zephyr OA/LP hashmap 作为临时集合；插入已有 key 返回 -EEXIST。
使用 k_realloc 系统堆分配器，所有路径调用 clear 释放桶，不使用 libc malloc。
删除比较函数、key 副本与 qsort；保留原表顺序及回调索引，不增加锁。
静态描述符用于串行启动登记，桶不保留；选择 SYS_HASH_MAP/OA_LP 和系统哈希函数。

DP、process native_sim 通过；覆盖首次分配/扩容分配失败、无节点发布、分配计数归零，
512 行首尾重复连续检查三次及成功路径归零。既有范围、回调、恢复默认及并发测试通过。
大表测试堆显式增加到 65536 B；产品原 2048 B DP 堆预算的 process 测试仍通过。
默认 MCUboot + IAPv3 构建及验签通过，Flash 87,660 B、RAM 37,280 B。
未执行实板验证或完整回归。
日志 /tmp/hash-dp-{build,test}.log、/tmp/hash-process-{build,test}.log、/tmp/hash-firmware.log。
用户原有两个未暂存修改块保留。
