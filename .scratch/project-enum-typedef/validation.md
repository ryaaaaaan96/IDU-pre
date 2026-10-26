# 全项目枚举使用 typedef

自有 C 代码中剩余的 19 个具名枚举、6 个匿名枚举统一改为 typedef enum，
新增别名使用 name_t，所有函数、结构字段、局部变量及类型转换引用同步更新。
已有参考接口的 typedef 名称不变，枚举成员和数值不变。
Zephyr 公共 API 的 enum modbus_mode 沿用上游声明。
AGENTS.md 加入约定以保持后续代码一致。

验证：bash tests/run.sh 完整回归退出码 0，日志包含 79 次 native 执行成功，
其他 Python/主机工具验证由脚本一并通过。git diff --check 通过。
日志位于工作区 build/enum-typedef-regression.log，未进行实物验证。
