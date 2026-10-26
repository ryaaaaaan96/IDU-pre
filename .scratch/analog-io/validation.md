# 模拟量模块与产品采样

按用户最终确认将 analog_input 重组为 analog_io，公开头文件 include/analog_io.h，
ADC 实现在 src/adc.c，接口 analog_adc_init/read。DAC 暂未实现，不提供空接口。
删除 input_sampler 组合模块；未提交的中间命名不保留。acquisition.c 整体按头文件、
硬件配置、输入初始化、输入采样、周期编排组织，直接调用模拟量与 GPIO 接口。
头文件全部位于文件顶部，产品不通过条件宏跳过采样。

测试随最终职责更新：移除已删除组合模块的专用测试，保留产品组合读取、通道顺序、
平均、位序、容量检查、硬件失败及重试验证；新增单通道 ADC 错误传播验证。
ADC 独立原生构建及测试通过，完整回归结果随后记录。未实板验证。

最终验证：完整 bash tests/run.sh 退出码 0；ADC 专项 14 测试通过，
完整回归包含 runtime 16 场景、acquisition 39 场景及其余模块验证。
git diff --check 通过。日志：工作区 build/analog-io-regression.log、build/analog-io-adc.log。
