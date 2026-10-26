# 验证记录

- ADC/产品采样：14 测试通过，覆盖按名称映射、平均、DI 位序、写点失败、ADC 失败与重试、
  电流零值/中点/饱和。产品 ADC/DI 配置中的 device_id/signal_id 经同步 DP 接口验证。
- 点表：53 测试通过、1 个现有可选测试跳过，包括同步写点立即从转发入口读到新值。
- 系统编排：39 个独立进程场景通过；运行期：16 个独立进程场景通过；任务看门狗：12 场景通过。
- MCUboot + IAPv3 板级固件编译、签名验证通过。最终构建结果随后补充。
- 完整脚本的调试 socket 测试受沙箱限制，本轮使用构建目录中的脚本副本明确排除此项，
  其余回归结果随后补充。LeakSanitizer 在 ptrace 沙箱中不可用，回归关闭泄漏检测。
- 未进行实物烧录验证。

日志位于工作区 build/mapped-input-{adc,points,acquisition,runtime,watchdog,firmware,regression}.log。

最终结果：排除沙箱不支持的调试 socket 测试后，其余完整回归退出码 0。
电流换算迁入循环后，重新验证 ADC/写点、真实点表同步读回和最终板级固件，均通过。
MCUboot 签名验证成功，SGZ 为 151888 字节。git diff --check 通过。
