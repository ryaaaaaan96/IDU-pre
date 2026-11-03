# 测试与验证

[文档导航](README.md) · [构建与升级](build-and-upgrade.md)

## 离线编译包验证

2026-11-02：在独立解压路径清空继承环境，使用新建 Python 3.14 虚拟环境、
包内 SDK 1.0.1 ARM GCC、CMake/Ninja 和离线 wheel，完成 MCUboot + IAPv3
全量构建、签名校验以及 HEX/BIN/SGZ 导出。没有使用原工作区虚拟环境或构建缓存。
Python 3.12/3.13 的离线依赖解析检查通过，未在这两个解释器上实际编译。
验证范围是同一 Linux 主机的隔离构建目录，不等同于新电脑或实板验证。

2026-11-03：根目录 `install-offline.sh` 使用 `--skip-system-deps --build`，
完成校验、全新解压、虚拟环境安装以及完整固件构建和验签；已有目录拒绝覆盖、
缺少压缩包的错误路径通过。系统依赖已安装，本次未执行 apt 安装。

安装器发行版适配：复现了“已有依赖但没有 apt-get 仍被拒绝”的错误；修复后同一
环境完成解压和虚拟环境安装。Ubuntu、Debian、Fedora 的包管理器分派通过模拟命令
验证，未实际执行 apt/dnf。Python 保留 Zephyr 要求的最低 3.12，移除人为上限。

设备树警告修正：SDK 1.0.1 下 MCUboot + IAPv3 pristine 构建和签名校验通过，
两个镜像不再报告 `unknown vendor prefix` 或 `avoid_unnecessary_addr_size`。
生成的 Flash 分区地址与容量保持不变；5 项 Sysbuild 配置测试通过。

## 测试入口

所有命令在 West 工作区根目录执行。完整回归：

```bash
CCACHE_DISABLE=1 ASAN_OPTIONS=detect_leaks=0 \
bash applications/IDU-pre/tests/run.sh
```

该入口包含 DP 核心、产品点表、参数存储、ADC、Modbus、transmit、串口接入、
IO、watchdog、IAP 和 Python 工具测试。关闭泄漏扫描只用于当前环境限制，不能替代泄漏检测。

单个模拟器套件：

```bash
source .venv/bin/activate
CCACHE_DISABLE=1 ZEPHYR_TOOLCHAIN_VARIANT=host west build -p always \
  -b native_sim/native/64 applications/IDU-pre/tests/transmit_stream \
  -d build/IDU-pre/tests/transmit_stream
build/IDU-pre/tests/transmit_stream/zephyr/zephyr.exe -flash_in_ram
```

`modbus_support` 和 `adc` 不带 `-flash_in_ram`；DP 存储跨启动场景使用
`tests/dp_storage/run.py`。其他变体及环境变量以 `tests/run.sh` 为准。
IAP 全部模式和跨启动结果日志测试使用 `tests/iap/run.sh`。

## Modbus 4.5.0 迁移专项

验证日期：2026-11-02；代码基线：`6ecdf3e`；Zephyr v4.5.0 / SDK 1.0.1。
本次只执行迁移涉及的专项，没有在 4.5.0 下执行完整回归。

| 对象 | 结果 | 覆盖内容 |
| --- | --- | --- |
| Modbus 默认队列 | 6/6 | RAW 同步调用、系统队列、协议校验、所有权与递归保护 |
| Modbus 专用队列 | 6/6 | 自有 RAW 保持同步，未占用接口进入官方专用队列 |
| transmit_stream | 21/21 | 判帧、轮转、映射、类型转换、字序、放缩、地址段 |
| protocol_port | 1/1 | APP 线程与 UART/GPIO 模拟接入、方向恢复、故障门禁 |
| iap-transmit | 指定升级用例 1/1 | 产品地址段、真实接收器、解包、验签、备用槽和试升级 |
| 合计 | 35 项通过 | 不包含其他版本的历史结果 |

IAP 组合测试其余 10 项按用例过滤跳过，不作为已执行项。
上游源码副本修改探针被哈希检查拒绝，确认版本保护仍有效。

## 保存、操作命令与对照点验证

产品点表新增保存命令、非持久对照值和操作命令，原持久参数和 Flash 布局未改。

- process_runtime 1/1，通过三行命令点的 key 与非持久标志，以及保存/操作命令的写回调检查。
- protocol_port 使用同一模拟 Flash 启动三次，各 1/1：第一次写周期/对照点为 600 并发保存命令，
  第二次验证持久点为 600、非持久点恢复初值 500、保存命令为 0，再发操作命令 2 验证周期
  立即回到 500 并保存，第三次验证恢复默认值后的 500 已跨启动保留。
- 自动保存间隔在该套件中设为一小时，防止后台保存掩盖命令路径。
- 注入 Flash 写失败验证异常 04 和成功重试；非法保存命令和非法操作命令均返回异常 03；
  命令 1 保存失败返回异常 04 且不复位；无变化重复保存不写 Flash。
- 操作命令 1（保存后冷重启，保存失败不复位）和 3（恢复出厂）会复位设备，未纳入自动套件，按
  [手动测试](modbus-test.md#8-操作命令重启恢复默认与恢复出厂) 验证。
- task_watchdog 26/26，新增显式重启场景：被阻塞的系统 workqueue 下仍按复位定时器冷复位，
  且不锁存故障、不改变运行阶段。
- IAP 完整升级组合用例 1/1，其他 10 项按过滤跳过。
- 完整 `tests/run.sh` 返回 0。io 套件的复位时延包装断言在 native_sim 上打印一次超过
  既有 30 ms 容差，但包装函数不改变退出码；该容差问题与本次命令改动无关，待单独收敛。

跨启动测试入口：`python3 applications/IDU-pre/tests/protocol_port/run.py <zephyr.exe>`。
本轮未执行实板测试；日志在 `build/IDU-pre/op-command-*.log`。

## 固件与资源

默认 MCUboot + IAPv3 构建、签名校验和 SGZ 导出通过，使用既有签名密钥。

| 镜像 | Flash | RAM |
| --- | ---: | ---: |
| IDU-pre | 106876 B | 78000 B |
| MCUboot | 37672 B | 18176 B |

SGZ 为 107744 字节；镜像摘要：
`f535f5aeacdd1eecdbd2895ab03528482f1393212fc21b3b6b88fc0dc07de86a`。
这些数字来自链接输出，不表示运行期栈、堆峰值或时间预算已经实测。
构建仍有自定义 vendor prefix 与设备树地址单元警告，尚未收敛。

日志当前位于 `/tmp/idu-modbus45-*-build.log` 和 `/tmp/idu-modbus45-*-test.log`；
日志可能随环境清理而消失。后续验收应同时保存版本、配置和执行结果。

## 尚待验证

- Zephyr 4.5.0 的全部测试套件，包括 DP/NVS、采样、watchdog 和 IAP 独立/配置模式。
- 实板串口长帧、持续收发、RS485 方向与恢复时序，旧上位机完整上传。
- 真实 Flash 保存、擦除、布局重建、出厂恢复和升级交换阶段断电。
- 外部看门狗边沿、队列阻塞停喂、HOLD 和 RESET 的实际行为。
- ADC 精度、工程量换算、实际采样周期、线程栈高水位及系统堆峰值。
- Modbus SERIAL 后端和多入口扩展；当前产品 UART 由 APP 独占。

主机测试、native_sim 和目标固件构建均不能代替实板验收。
