# GD32E505 SoC 接入

本模块补充当前 Zephyr 4.4.2 缺少的 `gd32e505` SoC 登记和早期时钟初始化，
复用 West 管理的 `hal_gigadevice` 及 Zephyr GD32 公共代码。它是硬件支持模块，
没有业务接口，也不复制厂商固件库。

```text
gd32e505/
├── zephyr/module.yml       # 声明 soc_root
└── soc/gd/gd32e505/         # SoC 元数据、Kconfig、构建和早期初始化
```

## 使用方式

在 `find_package(Zephyr ...)` 前把此目录加入 `ZEPHYR_EXTRA_MODULES`，
或使用 CMake 参数 `-DEXTRA_ZEPHYR_MODULES=/absolute/path/to/modules/gd32e505`。
模块元数据自动提供 `SOC_ROOT`；使用该 SoC 的板仍需提供自己的 DTS、引脚和分区。
本应用通过 [`../hardware.cmake`](../hardware.cmake) 注册硬件模块，
[`../modules.cmake`](../modules.cmake) 再注册点表和算法模块。

Sysbuild 在创建任何镜像前就需要发现 SoC，所以
[`bootloader/CMakeLists.txt`](../../bootloader/CMakeLists.txt) 也加载硬件模块，
并将模块列表写入 CMake 缓存，让 MCUboot 和主应用各自完成发现。
仅在应用的 CMakeLists 中注册，无法满足 MCUboot 的独立配置阶段。

本模块只通过 `soc_root` 扩展硬件目录，不注册全局源码或编译宏；
选择 `gd32e505` 才编译 SoC 实现，因此 `native_sim` 测试仍可使用同一模块清单。

## 支持边界

- 保留 `GD32E50X_XD` 和 FMC 字对齐编程兼容定义。
- 当前时钟方案限定 **20 MHz 有源输入、180 MHz CPU**：HXTAL bypass，
  PLL `/ 2 × 18`，AHB/APB2 180 MHz、APB1 90 MHz。编译断言检查输入和 CPU 频率。
  不宣称适配任意 E505 板卡或晶振方案。
- 兼容旧引导程序已打开 PLL 的情况：先切换 IRC8M，再重新设置时钟。
- 时钟等待失败时停止启动；复位或故障恢复方式由板级设计决定。
- PC13 外部看门狗属于 IDU 板，已归入
  [`boards/idu/idu_pre/board.c`](../../boards/idu/idu_pre/board.c)；
  仅 MCUboot 选择 `BOARD_EARLY_INIT_HOOK`。该钩子在 SoC 初始化后、
  `PRE_KERNEL_1` 驱动初始化前执行，应用的喂狗策略仍在 `app/hardware/health.c`。
- ADC 通道、采样平均和工程量换算仍由 IDU 应用配置；本模块不提供通用 ADC 驱动。

`sysbuild/` 是应用联合构建入口，`bootloader/` 的镜像配置和 Flash 分区也是产品策略，
不属于这个芯片模块。后续多个产品确实共享镜像编排时，可另用模块的
`sysbuild-cmake` 扩展点复用那部分逻辑。

依据：[Zephyr 模块构建设置](https://docs.zephyrproject.org/latest/develop/modules.html#build-settings)、
[Sysbuild 配置](https://docs.zephyrproject.org/latest/build/sysbuild/index.html)；
实际行为以工作区固定版本的 `scripts/zephyr_module.py`、`cmake/modules/zephyr_module.cmake`
和 `kernel/init.c` 为准。启动、时钟及看门狗仍需实板验证，见
[硬件说明](../../docs/hardware.md)和[验证记录](../../docs/testing.md)。
