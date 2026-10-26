# analog_io

模拟量输入输出模块，公开接口为 `<analog_io.h>`，通过 `CONFIG_ANALOG_IO` 启用。
当前实现 ADC 通道初始化和软件平均采样；DAC 尚未实现，实际需要时在 `src/dac.c` 扩展。

`analog_adc_init()` 接收调用者提供的通道组配置；`analog_adc_read()` 按配置顺序返回
平均后的原始值，检查容量和通道参数，失败时整批结果不可使用。配置和输出缓冲区由调用者持有，
不创建线程、不分配内存，调用者负责串行访问硬件。

数字输入与拨码不属于模拟量模块。应用 `app/acquisition/acquisition.c` 直接组合
`analog_adc_*` 与 `digital_io_*`，负责产品通道、位序、地址解释及样本有效性。
