# 初始化接口命名统一

基线：fc007fa。命名约定见 [docs/naming.md](../../docs/naming.md)。

## 修改

| 原接口 | 当前接口 |
| --- | --- |
| application_startup | application_init |
| iap_start | iap_init |
| iap_init(config) | iap_init_config(config) |
| watchdog_start | watchdog_init |
| dp_process_start | dp_process_init |
| dp_reference_start | dp_reference_init |
| co_start | co_polling_init |
| co_simulator_start | co_simulator_init |
| protocol_start_forwarding | protocol_forwarding_init |
| protocol_dp_tasks_prepare / protocol_co_tasks_prepare | protocol_dp_tasks_init / protocol_co_tasks_init |

初始化接口按一次性装配职责命名。实际放行线程的 protocol_dp_tasks_start / protocol_co_tasks_start、
运行期 dp_process_timer_start 和 Zephyr 原生启动接口继续表达启动操作。
同步模块声明、实现、应用调用、测试、Kconfig 帮助、AGENTS.md 和当前文档，不保留旧名转调入口。
历史验证和讨论保留当时名称。

## 验证

```bash
ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
```

- 两条命令退出 0；64 次原生程序执行成功，379 项原生测试和 23 项 Python 测试通过，主机 C 的 ASan/UBSan 检查通过。
- IAP 显式配置、项目配置、协议段接入及跨进程日志恢复均通过；DP、CO、看门狗及应用失败短路与重复初始化检查随完整回归通过。
- 31 个 C/头文件与基线进行标识符映射后比较：忽略注释和空白，代码一致；执行逻辑、参数、返回值、初始化顺序及错误策略不变。
- 当前源码、头文件和测试没有旧初始化接口调用；剩余 *_start 是线程或运行期定时器操作。
- MCUboot + IAPv3 构建及签名校验通过，BIN/HEX/SGZ 正常导出；最终构建与测试日志没有编译警告或错误。
- 应用 Flash 129728 B、RAM 104140 B，MCUboot Flash 36568 B、RAM 18096 B，与基线保持一致。
- 未修改协议、分区、密钥、参数存储格式或物理时序；本轮未做实板验证。

日志位于 build/IDU-pre/initialization-names/，产物位于 build/hex/IDU-pre-mcuboot-iapv3/，均不提交。
