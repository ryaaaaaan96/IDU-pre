# 合并 acquisition 文件

产品输入配置、初始化、采样与周期编排合并至 app/acquisition/acquisition.c；
样本结构、尺寸和入口声明统一至 acquisition.h。删除 config.c、sample.h，
更新应用、测试构建及文档引用。

产品源码无采样条件编译，固定包含硬件采样实现。编排测试在测试构建中提取同一源码的周期部分，
替换输入接口，不在产品中设置测试开关。
ADC 测试编译合并源码，使用仅声明维护接口的协议测试头，未使用的周期函数由链接器移除。

ADC 应用与驱动 16 测试通过；完整 tests/run.sh 验证结果见后续记录。
未进行实物验证。

最终验证：删除产品采样条件编译后，完整 bash tests/run.sh 退出码 0。
包含 ADC、runtime 16 场景、acquisition 39 场景及其余模块回归。
底层模块启用符号 INPUT_SAMPLER 保持必需启用；产品不提供不采样路径。
git diff --check 通过。
