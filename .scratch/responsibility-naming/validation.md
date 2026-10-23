# 职责命名与可配置采样迁移验证

基线：a88851b。范围依据 [已确认讨论](discussion.md)，当前约定见 [命名与采样边界](../../docs/naming.md)。

## 实施结果

- 应用入口、类型、日志、头文件及内部业务标识均按职责命名，包括 protocol_init、application_startup、acquisition_cycle、points_*、status_* 和 image_confirm_checkpoint。
- 原 idu_process.h 已改为 process/points.h；不保留旧接口别名。设备/信号/表标识改为 DEVICE_*、SIGNAL_* 和 TABLE_*，现有数值保持不变。
- input_sampler 独立组合 analog_input 和 gpio，使用调用者的配置、数量及输出缓冲区，不依赖产品节点、地址偏移或通道布局，也不分配输出内存或创建任务。
- 应用 acquisition/config.c 装配产品硬件，保留原引脚、采样顺序、平均次数、电气条件和拨码地址解释。样本声明归 acquisition/sample.h，移除只在旧测试夹具中填写的换算值数组，工程换算仍由 process 完成。
- GPIO 模块移除产品采样实现及 IDU_INPUTS 配置；ADC 平均次数配置移到应用 Kconfig，INPUT_SAMPLER 为新的通用模块开关。
- 更新构建、测试、当前文档与 AGENTS.md；历史记录保留当时名称。点表数组使用 *_rows，与 Iterable Sections 登记对象区分。

## 验证

在工作区根执行：

```bash
ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
```

- 完整脚本退出 0；64 次原生程序执行成功，379 项原生测试通过，0 失败；23 项 Python 测试通过，主机 C 的 ASan/UBSan 检查通过。
- 关闭 LeakSanitizer 仅适应执行环境，ASan 和 UBSan 保留；禁用 ccache 避免受限缓存路径。
- ADC 的 16 项测试包含新增的三项 input_sampler 边界：非产品通道布局及调用者缓冲区、容量/指针检查先于读取、仅模拟/仅数字配置及失败无效语义。
- 参数跨进程恢复、存储故障阶段命中、CO/TR 配置组合、镜像确认和 IAP 验证均随完整脚本通过。
- 当前 MCUboot + IAPv3 构建成功，BIN/HEX/SGZ 产物生成，签名验证通过；验证 JSON 的 signature_verified=true、iap_sgz_supported=true、hardware_verified=false。
- 应用 Flash 129728 B，较基线增加 216 B；RAM 104140 B，保持不变。MCUboot Flash 36568 B、RAM 18096 B，保持不变。
- 逐文件比较六个设备/信号/协议配置文件中的数值：与基线一致。参数存储头文件、存储实现、事务实现和产品分区定义逐字节一致。
- app 与 modules 的源码不存在 idu_* 函数，文件名中的产品前缀已清理；保留的产品设备树标识和应用 CONFIG_IDU_* 在约定范围内。
- 初轮构建发现并修正登记对象与表数组重名、旧测试仍引用换算值字段的问题；修正后完整回归和固件构建重新执行并通过，最终日志无编译警告或错误。

最终日志及生成产物均位于工作区 build/，未提交：

- build/IDU-pre/responsibility-naming/regression.log
- build/IDU-pre/responsibility-naming/firmware.log
- build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-validation.json

## 边界

本轮没有实板验证；引脚、电气输入、时序、运行时栈余量及真实升级链路仍需实测。
用户原有 app/protocol/protocol.c 的 getter 展开及空白改动保留在工作区，不纳入本轮提交。
