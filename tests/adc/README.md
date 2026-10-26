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

`application.c` 使用 Zephyr ADC/GPIO emulator 执行应用配置中的 `acquisition_inputs_sample()`，
验证通道顺序、每通道 10 次整数平均、DI/拨码位序及采样错误传播；
另测通用 `analog_io` 的独立通道分组、平均参数、缓冲区边界及读取错误传播。
产品组合采样由 `acquisition.c` 直接实现，验证 DI 与地址位序和 ADC 失败后的无效样本。
