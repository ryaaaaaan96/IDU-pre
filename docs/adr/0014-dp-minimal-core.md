# ADR 0014：DP 最小原子核心

状态：已接受（2026-10-27）

## 背景

产品只运行本地 process：采集通过同步 `dp_set_sig_value()` 写入，转发、异步样本、
参数下游读回和参考表目录都不参与运行。原 DP 模块仍有五套初始化入口、
异步队列/线程、定时器、参考表和 collect/transmit 描述符，源码约 5,800 行，
链接文本约 18 KB，堆预算 20.7 KB。

## 决定

把 `modules/dataprocess` 收缩为最小原子核心：

1. 唯一入口 `dp_register_model(model, params_area)`：业务层逐组登记；每组校验后
   按实际字数申请自己的点值块。参数区在首次访问参数接口时挂载。
2. 只保留单点同步写读：`dp_set_sig_value()`、`dp_get_sig_value()`、`dp_read()`。
3. 点表行只解释常量、本地点、持久参数、范围、读回调和命令回调。
   命令点执行 `handler`，不保存触发值；状态点执行 `read`。
4. 参数只做本机 NVS：保留 `parameter_store.c` 的记录/提交镜像格式，
   删除下游转发、待确认意图、读回恢复和去抖业务线程。
5. 删除异步样本队列、业务线程、定时器、参考表目录、`data_process.h` 字节适配层、
   collect/transmit 描述符及相关测试。
6. 删除不再可用的 `modules/collect`、`modules/transmit`、`modules/vport`
   和旧应用通信装配。

## 结果

- DP 源码从约 5,800 行降到约 1,300 行；模块只剩 `points.c`、`params.c`、
  `parameter_store.c` 和 5 个头文件。
- 产品应用 Flash 从 106,100 B 降到 88,852 B，RAM 从 78,676 B 降到 40,728 B；
  系统堆从 45,404 B 降到 16,988 B。
- 点表、参数键和 NVS 记录格式保持不变；已有参数记录可继续读取。
- 设置项改为本机同步持久化，掉电恢复不再依赖南向读回。

## 后果与未决

- 采集/转发和 IAP 路由不再是本产品能力。`modules/iap` 仍保留 `iap_route()`，
  但当前没有调用者；重新启用通信需要新增独立的 Modbus/RAW 路由适配层。
  IAP 最小路由计划在下次调试 IAP 配置时实现，见
  [.scratch/iap-route](../../.scratch/iap-route/spec.md)。
- 参数宽度/类型变化会按格式不兼容清空 `idu-settings`，与旧行为一致。
- 实板参数持久化、掉电和 IAP 上传需单独验证；本轮只有主机/native_sim 证据。
- 历史 ADR 0006、0008、0010 记录当时方案，不代表当前能力。

## 验证

见 [验证记录](../../.scratch/dp-minimal-core/validation.md) 和
[测试说明](../docs/testing.md)。

## 2026-10-28：收敛点表接口

替代上述模型登记及并行读接口：使用 `dp_register(params_area)` 一次登记分区，
`dp_register_table(table)` 逐张登记点表；读写统一为 `dp_set_para()` / `dp_get_para()`。
`struct dp_model_cfg` 改为 `struct dp_table`。删除兼容头 model.h、公共 dp_read、
dp_stop/dp_stopped 及 DP 故障停止状态。status 锁存整机阶段，主循环停止业务；
DP 不参与整机故障策略。

保留参数恢复默认和状态查询；删除无实际刷新/重试行为的 flush/recover 及空的取消复位。
IAP 直接查询存储是否就绪。同步写已经落盘，无需第二次 flush。
删除原刷新/恢复事务命令点（31092、31093），访问时返回点不存在，不复用这些 ID。
NVS 记录类型编码、逻辑键和镜像格式不变。

## 2026-10-28：设备树直接绑定参数分区

删除 dp_register() 和运行时分区登记状态。DP 强制要求 `dp-params` 设备树别名，
指向启用的固定分区；缺失时编译失败。应用只调用 dp_register_table() 登记点表。
板级节点及 label 从 idu_settings / idu-settings 改为 dp_params / dp-params；
保留外部 Flash 偏移 0x10000、大小 0x4000 及 NVS 内容格式，改名不擦除存储。
