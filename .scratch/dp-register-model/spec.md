# DP 逐组登记模型

Status: accepted
日期：2026-10-27

## 目标

把 DP 的一次性数组入口 `dp_register_models(models, count, params_area)` 改成
逐组登记 `dp_register_model(model, params_area)`，由业务层按处理表一组一组调用。
同时去掉 `dp_start` 隐含的“启动/冻结”生命周期语义。

## 决定

- 入口名 `dp_register_model()`，每次只接收一个 `struct dp_model_cfg`。
- 每组登记时校验行定义、累计点数/字数/参数预算，并按本组实际字数申请独立点值块。
- `params_area` 由首次登记确定，后续登记必须一致，否则 `-EINVAL`。
- 取消显式冻结步骤；参数区改为**首次访问参数接口时挂载**。
- 参数区挂载后再登记含持久参数的表返回 `-EBUSY`，业务层须先完成全部登记再读参数。
- 重复 `device_id` 返回 `-EEXIST`；点数/字数/参数超限返回 `-EINVAL`/`-ENOSPC`。
- 登记失败不回滚之前成功的组。
- `dp_stop()` / `dp_stopped()` 保持不变；停止后登记返回 `-ECANCELED`。

## 容量

新增 `CONFIG_DP_MODELS_MAX`，默认 4，控制登记槽位数。
其余预算仍由 `DP_POINTS_MAX_POINTS`、`DP_POINTS_VALUE_WORDS`、
`DP_PARAMS_MAX_COUNT` 跨组共享。

## 验收

- 产品点表仍为一张表，`product_models[]` 中是单元素指针数组，行为不变。
- `tests/dp_core` 覆盖：逐组登记、重复设备、参数区不一致、挂载后新增持久表、
  两组相同 `sig_id` 独立读写、按设备分别恢复参数、跨进程持久化。
- 默认固件构建无警告；`tests/run.sh` 通过。
- 文档、AGENTS、ADR 0014 同步新入口名。

## Comments

2026-10-27：用户认为 `dp_start` 名不副实，并希望“接口只注册一个，业务层逐个注册”。
本规格把命名和登记方式一起收敛。
