Status: resolved
Blocked by: None

# 已保存值替代 revision

按用户要求，将 revision 改为最后成功落盘值的比较，不增加锁，不提交。
仅持久点分配副本及 valid，副本和设备节点一次分配、一次释放。
加载时初始化副本；无记录保留 valid=false，未修改的初值不立即写 Flash。
保存前改回旧值可跳过写入；保存期间改回旧值仍按实际提交快照重新判断。
保存失败不更新副本，重新置 dirty 保证下一轮重试。
同时合并登记初始化循环，将配置与初值校验提前至已有持久点计数循环。

## 验证

- dp_core 和 process_runtime 线性/hash 各一套通过。
- dp_storage 线性/hash 各 11 个场景运行通过，周期保存线程场景通过。
- 新断言覆盖无记录有效标记、普通点无保存副本、重启初始化副本、改回已保存值跳过写入，
  以及 Flash 写入期间 RAM 回到旧值后仍保留待保存状态。
- 最终日志 /tmp/dp-final-{suite}-{normal,hash}-{build,test}.log；
  周期日志 /tmp/dp-storage-periodic-{build,test}.log。
- 产品默认固件构建及签名校验通过，/tmp/dp-saved-firmware.log。
  应用 Flash 90260 B / RAM 46064 B；签名 digest：
  26d0649ba2e23a8168d62b693d9005ed702e18662f9420aa3a47fbdfd619b9c0。
- clang-format dry-run 和 git diff --check 通过。
- 本轮未重跑 ADC、无上传固件或完整回归，未进行实板验证。
- 两把已有锁保持私有且数量不变，没有新增锁；未暂存、未提交。

## 登记顺序修正

按用户澄清，计数循环不再校验；设备存储先挂载，再逐点校验、设置初值、加载历史值。
保留原有逐点校验，错误时释放 RAM，已经分配的持久槽位保留供重试。

登记顺序调整后，dp_core、process_runtime、dp_storage 线性/hash 和周期线程测试重新通过；
运行日志沿用 /tmp/dp-final-*，构建日志为 /tmp/dp-attach-firmware.log。仍未提交。

## 最终确认：恢复两个循环

保留已保存值替换 revision，只撤销登记循环调整。计数后分配内存，第一遍校验/设置初值，
挂载存储，再第二遍加载/校验历史值。无效配置不会提前分配 Flash 槽位。仍不提交。
