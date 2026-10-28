# 公共基础类型

通过 `<common/value.h>` 使用 `value_t`，只依赖标准整数类型和
`CONFIG_COMMON_VALUE_WORDS`，不依赖 DP 或其他业务模块。

联合体提供数值、内嵌数组和外部只读数组引用视图；使用方决定类型、长度及指针生命周期。
它不是自动转换或序列化接口。DP 的 initial/minimum/maximum 及运行值共用此类型；
DP 大于四字节初值仍用 `.array`，运行数据使用 `.words`/`.bytes`。
浮点成员不表示 DP 已支持浮点范围检查或持久化。

`value_type_t` 与 `VALUE_TYPE_*` 描述数值类型，可被各模块独立使用。
联合体包含 f32 视图，但当前类型枚举没有浮点项，需实现完整业务支持后再扩展。
