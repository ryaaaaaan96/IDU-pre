# 测试与验证

[文档导航](README.md) · [构建与升级](build-and-upgrade.md)

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

## 当前已记录结果

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

## 固件与资源

默认 MCUboot + IAPv3 构建、签名校验和 SGZ 导出通过，使用既有签名密钥。

| 镜像 | Flash | RAM |
| --- | ---: | ---: |
| IDU-pre | 105524 B | 77944 B |
| MCUboot | 37560 B | 18184 B |

SGZ 为 106384 字节；镜像摘要：
`a64cb96b679c3e6ee23ada2464f0b25f412224bf5384103d997a5304344e8f09`。
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
