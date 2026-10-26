# 应用直接使用 Zephyr GPIO

删除 modules/gpio、digital_io_config、DIGITAL_IO 配置及构建依赖。
应用直接使用 gpio_is_ready_dt、gpio_pin_configure_dt、gpio_pin_get_dt，
分别初始化 DI 和地址输入，并按设备树位序组合逻辑值；GPIO_ACTIVE_LOW 和上下拉继续由
设备树与 Zephyr GPIO 接口处理。失败保持样本无效，不运行 ADC 或发布有效快照。

移除已删除模块的通用测试，保留外部看门狗测试，新增逐路验证 16 路 DI 和 4 路拨码位序。
验证：ADC 驱动/产品采样、看门狗、runtime 16 场景、acquisition 39 场景、构建配置 8 测试通过。
MCUboot + IAPv3 板级固件编译及签名验证通过，SGZ 152352 字节。
产物：工作区 build/hex/IDU-pre-mcuboot-iapv3。

完整 tests/run.sh 在调试会话测试因沙箱禁止创建 socket 而中断，未宣称全量通过。
LeakSanitizer 在当前沙箱 ptrace 环境不可用，重试时设置 ASAN_OPTIONS=detect_leaks=0；
本次相关原生测试均独立通过。未实板验证。
日志：build/direct-gpio-regression.log、build/direct-gpio-focused.log、
build/direct-gpio-acquisition.log、build/direct-gpio-firmware.log。
