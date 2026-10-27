# ADR 0014：DP 最小原子核心

状态：已接受（2026-10-27）

## 背景

产品只运行本地 process：采集通过同步 `dp_set_sig_value()` 写入，转发、异步样本、
参数下游读回和参考表目录都不参与运行。原 DP 模块仍有五套初始化入口、
异步队列/线程、定时器、参考表和 collect/transmit 描述符，源码约 5,800 行，
链接文本约 18 KB，堆预算 20.7 KB。

## 决定

把 `modules/dataprocess` 收缩为最小原子核心：

1. 唯一入口 `dp_start(models, count, params_area)`：校验模型、按实际字数申请点值、
   挂载参数区并冻结配置。
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
- 参数宽度/类型变化会按格式不兼容清空 `idu-settings`，与旧行为一致。
- 实板参数持久化、掉电和 IAP 上传需单独验证；本轮只有主机/native_sim 证据。
- 历史 ADR 0006、0008、0010 记录当时方案，不代表当前能力。

## 验证

见 [验证记录](../../.scratch/dp-minimal-core/validation.md) 和
[测试说明](../docs/testing.md)。
