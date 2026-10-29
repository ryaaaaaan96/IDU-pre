Status: resolved
Blocked by: None

# 测点运行数据私有化

将 dp_point_data_t 从公开 dataprocess.h 移入 src/dp_internal.h。
保留字段、内存布局和行为；应用仅使用点表配置及 DP 接口。

## 验证

dp_core、process_runtime 线性/Hash 均通过；dp_storage 各 45 个场景与周期保存通过。
格式和 diff --check 通过。未验证实板，未提交。
日志：/tmp/dp-private-data-validation.log、/tmp/dp-final-*-test.log。
