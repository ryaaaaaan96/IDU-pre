# DP 最小原子核心：验证记录

状态：2026-10-27，桌面构建与 native_sim 验证完成；实板未验证。

## 变更规模

| 项 | 调整前 | 调整后 |
| --- | ---: | ---: |
| DP 源码行数 | 5,827 | 1,056 |
| 应用 Flash | 106,100 B | 88,852 B |
| 应用 RAM | 78,676 B | 40,728 B |
| 系统堆 `kheap__system_heap` | 45,404 B | 16,988 B |
| DP 堆预算 | 20,736 B | 4,608 B |
| DP 静态池 | 采样池等约 4.7 KB | 无异步池 |

DP 堆预算为 `DATAPROCESS 512 + DP_PARAMS 4096`；IAP 仍为 12288 B。

## 验证

| 项 | 结果 |
| --- | --- |
| 默认 `--pristine always` 构建 | 通过，无编译警告 |
| MCUboot 签名校验 / SGZ 导出 | 通过，SGZ 89,632 B |
| `tests/dp_core` | 通过：常量、本地写读、范围、命令点、状态读、停止 |
| `tests/dp_core` 跨进程参数 | 通过：写入 700 → 新进程读取 700 → 恢复默认 500 |
| `tests/process_runtime` | 通过：产品模型登记、参数区就绪、DI 同步写读、重复启动拒绝 |
| `tests/adc` | 6 + 8 项通过 |
| `tests/io` | 10 个独立进程场景通过 |
| `tests/modbus_support` | 3 项通过 |
| `tests/task_watchdog` | 25 个独立进程场景通过 |
| `tests/iap/run.sh` | 通过，2 项跨进程探针按设计跳过 |
| `test_mcuboot_image.py` | 通过 |
| `test_mcuboot_artifacts.py` | 通过（签名、HEX/工厂范围、5 项拒绝） |
| `test_build_configuration.py` | 5 项通过 |
| `test_debug_session.py` | 4 项通过 |
| `git diff --check` | 通过 |

构建日志：`build/IDU-pre/dp-minimal-build.log`；
测试日志：`build/IDU-pre/dp-core-build.log`、`build/IDU-pre/iap-minimal.log` 等。

## 行为变化

- 参数写入同步提交 NVS，不再有 1 秒去抖/业务线程。
- 删除异步样本、参考表、collect/transmit 描述符、下游参数读回和意图日志。
- 设置项只保存在本机；掉电恢复不再依赖南向设备。
- `modules/collect`、`modules/transmit`、`modules/vport` 已删除。
- IAP 模块仍初始化，但当前没有 TR/Modbus 路由，串口上传不可用。

## 未验证

- 实板参数写入/掉电恢复、NVS 磨损和旧记录迁移。
- 实板 IAPv3 上传（当前无路由）。
- 堆峰值、栈高水位和长时间运行。
- 删除采集/转发后的现场联调行为。
