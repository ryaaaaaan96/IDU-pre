Status: resolved
Blocked by: None

# 实现四个读写入口

验证两种入口互通、index 首尾与越界、空值、设备错误、回调上下文、
回调失败及范围检查；执行 DP 核心、产品装配和存储回归。

## 验证结果

DP 核心、process_runtime、dp_storage 的线性与设备 Hash 模式通过，
存储各 70 场景，另有周期保存和 ADC 套件通过。默认固件构建及验签通过。
clang-format 与 git diff --check 通过。实板未验证。
日志：/tmp/dp-key-index-validation.log、/tmp/dp-final-*-test.log、
/tmp/dp-key-index-adc-test.log、/tmp/dp-key-index-firmware.log。
