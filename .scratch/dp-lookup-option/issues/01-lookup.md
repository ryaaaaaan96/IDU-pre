Status: resolved
Blocked by: None

# 验证

新增 CONFIG_DP_HASH_LOOKUP 默认 n，仅切换设备 ID 的运行期查找：
关闭时线性；开启时设备 hashmap 平均 O(1)。设备内 signal_key 始终线性，原行索引不变。
按用户澄清保留头插，删除尾指针和设备顺序维护，不增加测点索引。
临时重复检查不受开关影响。设备索引不可变，登记仍启动串行，无额外锁。
设备索引分配或扩容失败释放未发布节点，已登记设备仍可读。

验证：DP 核心与 process 产品装配各自 on/off 两种 native_sim 配置均通过。
包括设备索引分配失败穷举、已有设备不受影响、重复与非法 key、回调索引一致、
恢复默认及并发点值快照。临时 hashmap 清理与分配计数按常驻索引基线检查。
两种模式 21 点产品表均使用默认 2048 B 堆测试通过，测试入口已加入双模式。
默认 MCUboot + IAPv3 on/off 两种构建及验签通过，最终产物恢复为关闭配置。
关闭 Flash 87,516 B / RAM 37,280 B；开启 Flash 87,612 B / RAM 37,320 B。
日志 /tmp/lookup-{off,on}-build.log、/tmp/lookup-{off,on}-test.log、
/tmp/lookup-product-{off,on}-{build,test}.log、/tmp/lookup-firmware-{off,on}.log。
bash -n tests/run.sh 通过；未执行实板或完整回归。

## Comments

会话期间用户更新了当前 HEAD 并吸收既有格式修改，本轮以最新 HEAD 为基线，不回退这些内容。
