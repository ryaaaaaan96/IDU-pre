# DP 最小原子核心

Status: accepted
日期：2026-10-27

## 目标

把 `modules/dataprocess` 收缩成当前产品真正执行的最小核心：

**模型登记 → 单点同步写 → 单点读 → 可选参数持久化**

每个能力只保留一个入口，不再为未运行的采集、转发、异步样本和参考平台接口
预留分支。

## 已确认边界

用户选择“最小核心，但保留持久参数”：

- 保留：模型登记、同步写点、同步读点、命令点回调、读回调、参数 NVS 持久化、
  恢复默认、参数状态。
- 删除：异步样本队列/任务/完成通知、`dp_submit_*`/`dp_publish`、collect/transmit
  描述符及其校验、参考表/provider/table registry、`data_process.h` 字节适配层、
  定时器/业务线程/reboot 编排、事件观察者、下游参数转发与意图日志。
- 设置项（采样周期、标签、站号）改为在本机 NVS 持久化，不再依赖南向读回。

## 事实基线

| 项 | 当前 |
| --- | --- |
| 模块源码 | 5,827 行 |
| 最终链接 DP 文本 | 17,982 B |
| DP `.bss` + `.noinit` | 约 7 KB |
| DP 堆预算 | 20,736 B |
| 相关测试 | 约 4,674 行 |

## 目标结构

```
include/dataprocess/
  reference_tables.h   行类型、数据联合、标志（保留文件名，内容最小化）
  points.h             值/状态/模型/写读/停止
  params.h             参数状态与持久化接口
  model.h              points.h 的兼容入口
src/
  points.c             模型校验、点值存储、单点写读
  params.c             NVS 参数缓存、提交、恢复
  parameter_store.c    NVS 记录/提交镜像（保留现有格式）
```

预计模块约 1,400 行、链接文本约 6 KB。

## 行为变化

- 参数写入在调用线程同步完成 NVS 提交，不再有 1 秒去抖和业务线程。
- 下游参数转发、待确认意图、读回恢复不再属于 DP；`modules/collect` 若要重新
  接入，需要新的适配层。
- 采集/转发模块不再由产品配置启用；IAP 路由问题保持现状，另行处理。
- 点表行回调改为原生 `(key, value)` / `(key, state)` 签名，不再使用
  `FUNC_PARA_ALL`。

## 验收

- 默认 MCUboot + IAPv3 构建通过，无警告。
- 产品镜像中不再出现 `dp_publish`、`dp_submit_*`、`dp_reference_*`、
  `dp_table_registry_*`、`data_process` 相关符号。
- 参数在模拟 Flash 上跨进程持久化、恢复默认、范围拒绝、参数状态读取通过。
- `git diff --check` 通过；实板未验证。

## Comments

2026-10-27：用户明确目标为“最小的原子性设计，内容尽可能小”，并选择保留
持久参数。本规格替代 `.scratch/dp-optimization/` 中的渐进优化清单；
原清单保留为审查记录。
