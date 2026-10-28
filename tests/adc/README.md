# ADC 回归

```bash
source .venv/bin/activate
ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
  applications/IDU-pre/tests/adc -d build/IDU-pre/tests/adc
timeout 30s build/IDU-pre/tests/adc/zephyr/zephyr.exe
```

`driver.c` 编译实际驱动，通过公开 `adc_*` API 测试参数边界、结果缓冲区、
校准/转换超时、失败恢复、初始化错误传播及并发通道隔离。
仅 HAL、MMIO、时钟/复位和 pinctrl 由测试替身提供，不模拟实际芯片的电气行为。

`application.c` 使用 Zephyr ADC/GPIO emulator 执行实际 `acquisition_cycle()`，
截获 `dp_set_para()` 检查目标设备、信号和写入值。覆盖按名称选择非顺序 ADC 通道、
10 次整数平均、DI 位图、逐路位序、ADC 失败/重试、DP 写点失败，以及电流换算的零值、
中点与饱和边界。地址拨码不参与采样。每点独立同步提交，失败时此前完成的点保留新值。
