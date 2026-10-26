# 应用直接使用 Zephyr ADC

按用户要求删除 analog_io 模块、analog_adc_config 及其构建/配置引用。
应用通过 adc_is_ready_dt 和 adc_channel_setup_dt 初始化通道，使用
adc_sequence_init_dt、adc_read_dt 同步读取，保留产品通道名称映射、软件平均、
整数截断、错误返回及整批样本无效语义。数字输入与地址仍由 gpio 模块负责。
ADC 平均次数依赖 CONFIG_ADC，不再引入包装模块开关。

删除已移除通用模块的专用测试，产品读取、非顺序通道映射、平均、失败重试、
DI/拨码位序及驱动边界测试继续保留。
验证：ADC 11 测试、构建配置 8 测试、runtime 16 场景、acquisition 39 场景通过；
git diff --check 通过。未进行实物验证。
日志位于工作区 build/direct-adc-validation.log、build/direct-adc-acquisition.log。
