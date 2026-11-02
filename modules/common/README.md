# 公共基础类型

通过 `<common/value.h>` 使用 `value_t`，只依赖标准整数类型和
`CONFIG_COMMON_VALUE_WORDS`，不依赖 DP 或其他业务模块。

联合体提供数值、内嵌数组和外部只读数组引用视图；使用方决定类型、长度及指针生命周期。
它不是自动转换或序列化接口。DP 的 initial/minimum/maximum 及运行值共用此类型；
DP 初值和运行数据均使用内嵌联合体，不通过 `.array` 解析初值。
浮点成员不表示 DP 已支持浮点范围检查或持久化。

`value_type_t` 与 `VALUE_TYPE_*` 描述数值类型，可被各模块独立使用。
联合体包含 f32 视图，但当前类型枚举没有浮点项，需实现完整业务支持后再扩展。

## 通用放缩

`<common/scale.h>` 提供 `value_scale_t`：numerator、denominator、offset。
正向 `output = input * numerator / denominator + offset`，反向先减 offset 再乘分母除分子。
`value_scale_apply(scale, inverse, input, output)` 使用 int64_t 中间值，整数除法向零舍入；
分子分母为零返回 -EINVAL，运算溢出返回 -ERANGE，失败保持输出不变。
NULL 配置按恒等处理；调用方可先检查指针直接跳过调用，无需额外 flag。
该类型不携带协议或 DP 内部类型；transmit 统一使用 external_type，不单独配置计算类型。

## 整数视图辅助接口

value_integer_bits 返回整数位宽，value_integer_get 按声明类型提取整数到 int64_t，
value_integer_from_bits 从低位构造指定整数并按补码解释有符号值。
实现统一位于 src/value.c，不解释协议字序、放缩或 DP 点表。
value_can_convert 判断支持的类型组合，value_convert 统一执行完整转换。
整数类型可互转，收窄保留低位并按补码解释符号；STRING/BYTES/WORDS 只允许同类型复制。
支持原地转换；空指针返回 -EINVAL，非法类型或不支持的组合返回 -ENOTSUP，失败保持输出不变。
DP 调用公共判断与转换接口，不再维护私有转换逻辑；transmit 继续复用整数视图辅助接口。
