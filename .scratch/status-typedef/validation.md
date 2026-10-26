# 状态枚举类型与动作命名

三个状态枚举改为 typedef：status_phase_t、status_fault_phase_t、status_action_t。
动作常量使用 STATUS_ACTION_CONTINUE / STATUS_ACTION_HOLD / STATUS_ACTION_RESET，
与阶段常量区分，数值与执行策略保持不变。接口、调用点、测试和领域文档同步更新。
头文件沿用用户已展开的枚举排版。

看门狗测试显式进入 STATUS_RUN，验证运行期超时复位，避免沿用启动阶段导致留驻。
验证：runtime 16、acquisition 39、task_watchdog 12 个独立进程场景全部通过；
修改空白检查通过，未实板验证。
