Status: resolved
Blocked by: None

# 删除运行计时策略

startup 完成必要初始化后直接调用 boot_write_img_confirmed，再进入 STATUS_RUN。
删除 image_confirm.c/.h、status_progress、旧策略测试目录及回归入口。
确认失败上报 WATCHDOG_ACTION_HOLD，不复位、不开始采样；普通初始化失败不会调用确认。

## Answer

- acquisition：40 个独立进程场景通过，验证启动顺序、初始化失败不确认、确认失败留驻、成功只确认一次、采样间隔变化与存储降级/待恢复不阻止确认。
- runtime：16 个独立进程场景通过，覆盖正常、降级、debug 和 UART 初始化失败。
- task_watchdog/run_modes.sh：50 个独立进程场景通过，清理对 status_progress 的旧引用。
- test_build_configuration.py：8 项通过；tests/run.sh 的 bash -n 和 git diff --check 通过。
- MCUboot + IAPv3 固件构建和签名验证通过，真实 boot_write_img_confirmed 链接成功。
- 产物：工作区 build/hex/IDU-pre-mcuboot-iapv3/；SGZ 151744 字节。
- 未进行实板烧录、换版或回退验证。
