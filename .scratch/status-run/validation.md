# 状态初始化与运行阶段命名

- status_start 改为 status_init，声明、调用和当前文档同步更新。
- 原 STATUS_WAITING 改为 STATUS_RUN，枚举值保持不变。
- 状态初始化期间保留 STATUS_STARTING；application_startup 在硬件、IAP 和协议服务全部成功后进入 STATUS_RUN。
- acquisition_cycle 从 STATUS_RUN 开始，完成采样、发布及健康检查后返回 STATUS_RUN；故障继续锁存，不能切回运行。
- 既有成功路径测试增加初始化成功和采样周期完成后处于 STATUS_RUN 的明确断言。

验证：

```bash
CCACHE_DISABLE=1 ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/acquisition -d build/IDU-pre/tests/acquisition
timeout 30s build/IDU-pre/tests/acquisition/zephyr/zephyr.exe
CCACHE_DISABLE=1 ./applications/IDU-pre/build.sh
```

17 项 acquisition 测试通过，0 失败；MCUboot + IAPv3 固件构建与签名校验通过，最终编译日志无警告或错误。
日志保存在 build/IDU-pre/status-run/。没有实板验证，本轮未修改引脚、时序或参数存储格式。
