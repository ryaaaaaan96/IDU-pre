# 收敛 DP 初始化入口与错误诊断

Status: open
Blocked by: 05
优先级：P2

## 目标

把当前五套初始化/登记入口收敛成一条可验证的装配路径，同时保留参考接口的错误
语义；让启动失败时能看到真实 errno，而不是统一的 `-1`。

## 证据

- `dp_init()`：读取模型、原生 provider 或参考表三种配置源，内部调用
  `dp_init_config()`，失败统一 `return -1`。
- `dp_init_config()`：直接接受 `struct dp_equip_cfg`，供测试和参考端口使用。
- `dp_params_init(cfg, area_id)`：再次调用 `dp_validate(cfg)`，并维护自己的
  `static const struct dp_equip_cfg *equipment`。
- `dp_process_init(plan)`：要求 `dp_config()` 已就绪，执行 INIT 和定时器计划。
- `dp_reference_init()`：执行全局 INIT/TIMER 登记表。
- `dp_params_init()` 没有校验传入 `cfg` 与 `dp_config()` 相同；两者不一致时，
  点值层和参数层会使用不同配置。
- 产品 `process_runtime.c` 只走 `dp_register_models → dp_init → dp_process_init`，
  其余入口仍编译保留。

## 方案

1. 明确定义“配置阶段 / 存储阶段 / 业务阶段”的顺序，并在头文件中按顺序列出。
2. `dp_params_init()` 拒绝 `cfg != dp_config()`，或改为只读取 `dp_config()`。
3. 增加一个不改变参考 ABI 的诊断入口，例如 `int dp_init_error(void)` 返回最近
   一次初始化的 errno；`dp_init()` 仍返回 -1 以兼容参考接口。
4. 评估新增一个产品侧封装 `dp_start(models, count, params_area, plan)`，把
   模型登记、`dp_init`、可选参数挂载、`dp_process_init` 串起来；参考路径保留。
5. 在 `docs/` 中画一张初始化顺序图和失败回滚边界。

## 验收

- 传入与 `dp_config()` 不同的 `cfg` 时，`dp_params_init()` 返回 `-EINVAL`。
- 启动失败日志能显示具体 errno（至少 `-EINVAL`、`-ENOMEM`、`-ENOTSUP` 可区分）。
- `process_runtime`、`reference_config`、`reference_ports`、`points` 通过。
- 初始化顺序图与实际代码一致；重复初始化仍返回 `-EALREADY`。

## 风险

- 新增 `dp_start()` 会与现有 `dp_init()` 形成两条路径，若不做清楚包装，反而增加
  入口数量；优先做 2 和 3，`dp_start()` 只在产品侧有明确收益时再加。
- `dp_init()` 的 `-1` 是参考接口约定，不能直接改成 `-errno`。
