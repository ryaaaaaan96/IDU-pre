# 时钟配置构建检查

需要已安装 Arm 工具链，从工作区根目录运行：

```bash
CCACHE_DISABLE=1 bash applications/IDU-pre/tests/clock/run.sh
```

脚本使用上游 `hello_world` 和本地硬件模块，验证三种配置：

- `120mhz.overlay`：8 MHz 晶振、PLL `/2 ×30`、120 MHz CPU，构建成功。
- `mismatched-cpu.overlay`：CPU 声明与 PLL 输出不一致，必须因指定断言失败。
- `overclocked-apb1.overlay`：APB1 为 180 MHz，必须因 90 MHz 上限断言失败。

失败用例同时检查诊断原因，工具链缺失等其他构建错误不算通过。
输出在 `build/IDU-pre/clock-*`，日志在 `build/IDU-pre/tests/clock-*.log`。
该脚本只编译，不烧录；本板的实际有源输入仍为 20 MHz。
