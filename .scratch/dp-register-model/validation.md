# DP 逐组登记模型：验证记录

状态：2026-10-27，主机与 native_sim 验证完成；实板未验证。

## 接口变化

| 项 | 调整前 | 调整后 |
| --- | --- | --- |
| 登记入口 | `dp_register_models(models, count, params_area)` | `dp_register_model(model, params_area)` |
| 调用方式 | 一次传数组 | 业务层逐组调用 |
| 参数区挂载 | 登记时立即挂载 | 首次访问参数接口时挂载 |
| 追加登记 | 不支持（`-EALREADY`） | 支持；参数区已挂载后禁止新增持久参数（`-EBUSY`） |
| 点值存储 | 单块连续分配 | 每组独立分配 |

## 资源

| 项 | 调整前 | 调整后 |
| --- | ---: | ---: |
| 应用 Flash | 88,852 B | 88,720 B |
| 应用 RAM | 40,728 B | 40,776 B |
| MCUboot Flash / RAM | 36,868 / 18,096 B | 不变 |
| SGZ | 89,632 B | 不变 |

新增 `CONFIG_DP_MODELS_MAX=4`，登记槽位 32 B；其余预算不变。

## 验证

| 项 | 结果 |
| --- | --- |
| 默认 `--pristine always` 构建 | 通过，无编译警告 |
| `tests/dp_core` | 通过：逐组登记、重复设备 `-EEXIST`、参数区不一致 `-EINVAL`、挂载后新增持久表 `-EBUSY`、两组同 `sig_id` 独立读写、按设备分别恢复 |
| `dp_core` 跨进程 | 通过：DEV 700 / DEV_ALT 800 持久化，新进程分别恢复默认 500 / 600 |
| `tests/process_runtime` | 通过：`product_models[]` 单表登记、参数区就绪、DI 读写、停止后拒绝 |
| `tests/run.sh` | 通过，含 adc、io、modbus_support、task_watchdog、iap、MCUboot 与 Python 检查 |
| `git diff --check` | 通过 |

## 未验证

- 实板参数写入/掉电恢复和 NVS 磨损。
- 多张表同时运行时的堆峰值与长时间稳定性。
- 实板 IAP 上传（仍无路由）。
