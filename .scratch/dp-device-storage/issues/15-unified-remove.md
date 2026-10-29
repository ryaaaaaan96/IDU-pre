Status: resolved
Blocked by: None

# 统一参数删除入口

将 dp_params_erase_all 合并为 dp_params_remove 的 DP_DEVICE_ID_ALL 分支。
单设备删除保留断电恢复流程，全部删除清理整个分区并由调用方重启。

## 验证

dp_core、process_runtime 线性/Hash 均通过；dp_storage 两种模式各 45 个场景通过，
周期保存通过。clang-format 和 diff --check 通过。未验证实板，未提交。
日志：/tmp/dp-unified-remove-validation.log、/tmp/dp-final-*-test.log。
