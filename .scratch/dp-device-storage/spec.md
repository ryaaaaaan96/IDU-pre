# DP 设备持久化

Status: resolved
Blocked by: None

## 规格

用户确认 dp_init、register_device/remove_device、set/get、restore_defaults、阻塞 flush。
设备分区独立，DTS 总区内部管理映射，NVS lookup cache，私有线程每五秒保存 dirty/revision。
删除释放 RAM 和存储，可重新登记；全部 DP 业务先静止，后台保存与目录变更串行。

## 实现补充

原区不能向后覆盖 IAP，故旧数据保留，新区 0x1c000/64 KiB。
挂载前固定首扇区识别布局，管理 8 KiB、六个设备槽位各 8 KiB。
产品采样周期、标签、保存地址启用持久化；其余为 RAM 点。
映射写错误冻结拓扑修改等待重启核对，不自动擦除未知内容。

## 用户补充

- 不提供 CONFIG_DP_PERSISTENCE：启用 DP 就需要持久化区域，纯 RAM 设备不占槽位。
- storage_type 移入 common/value.h，命名 value_storage_type，供模块共用稳定类型编码。

## 验证

执行结果记录在 issues/01-storage.md；模拟器和实板证据分开。

## 交付状态

实现和相关验证完成；按用户要求保持未提交，等待查看修改。

## 已保存值替换

用户要求用最后成功保存值替代 revision，且合并登记时两次初始化循环。
本轮仍不提交，见 issues/02-saved-value.md。

## 出厂恢复补充

新增 dp_factory_reset(device_id, before_reboot, context)，清空目标设备存储、锁外回调后冷重启。
默认恢复跳过标志改为 SKIP_DEFAULTS，出厂恢复不跳过任何持久点；详见 issues/11-factory-reset.md。

## 出厂恢复并发与全部选择补充

出厂恢复只禁止 Flash 保存，不阻止 RAM 读写/默认恢复；无需提前停止业务。
不允许并发登记或删除设备。DP_DEVICE_ID_ALL=0 清空整个 DP 总分区，包括历史未登记设备。
验证及边界见 issues/12-factory-ram-access.md。

## 删除入口收敛

删除 begin/finish，仅保留存储层 dp_params_remove，内部直接完成所有 Flash 清理。
出厂恢复不置空 RAM 中的 storage；普通移除改为存储成功后再释放 RAM，失败暂停保存。
见 issues/14-merge-remove.md。

## 统一删除选择

dp_params_remove 接受普通设备 ID 或 DP_DEVICE_ID_ALL，移除独立整区擦除入口。
不改变 Flash 清理、失败处理或重启行为，见 issues/15-unified-remove.md。

## 参数异常恢复

保存失败失效 saved_valid，保留 dirty 至成功重试；登记缺失参数立即追加初值，
不兼容或越界警告并追加初值，真正读取错误仍返回；不改变单设备出厂恢复前置条件。
见 issues/17-storage-recovery.md。

## 未登记设备出厂恢复

取消单设备出厂恢复的 RAM 登记前置条件，支持登记失败后直接清理历史槽位并重启。
没有槽位也执行可选回调并重启；设备已登记时仍先暂停保存。见 issues/18-unregistered-reset.md。
