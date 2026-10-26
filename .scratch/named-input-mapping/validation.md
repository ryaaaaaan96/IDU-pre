# 按设备树名称选择输入

按用户要求删除产品采样配置中的显式数字索引。ADC 使用 io-channel-names 和
ADC_DT_SPEC_GET_BY_NAME；DI 与拨码分别使用具名 GPIO 属性及 GPIO_DT_SPEC_GET。
同步板级设备树、绑定和 ADC 测试夹具。当前使用 current-1..8、di1-gpios..di16-gpios、
address1-gpios..address4-gpios 作为可替换名称，不更改物理通道、引脚或产品样本位序。
后续业务重命名需同步设备树/绑定与应用引用，保持编译期缺失名称检查。

验证：测试设备树故意打乱 ADC 引用与对应名称的列表顺序；14 个驱动/产品采样测试
全部通过，证明产品使用名称选择而不依赖设备树条目位置。diff 空白检查通过。
未进行实物验证。
