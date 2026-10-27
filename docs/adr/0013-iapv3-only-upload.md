# 0013：只保留 IAPv3 镜像上传入口

状态：已接受。用户确认删除 SMP 上传配置，并要求同步修改构建入口、测试与文档。

## 背景

应用此前提供三个互斥的升级入口选项：默认 IAPv3（RS485/Modbus）、`--smp-uart`
（USART0/SMP）和 `--no-upload`。SMP 由 Zephyr MCUmgr 提供，需要额外的
`conf/smp-uart.conf` 和设备树 chosen 绑定，并把 USART0 的 RX 从 Shell 让给 SMP。
应用源码从未引用 mcumgr；镜像确认由 MCUboot 的 `boot_write_img_confirmed()`
完成，与上传通道无关。

两条通道在 Kconfig 中互斥（`IAP`、`IDU_IAPV3` 都带 `!MCUMGR_GRP_IMG`），
因为二者都会写 slot1。现场升级使用 RS485，SMP 只在接调试头时可用，属于研发辅助，
不构成产品能力。

## 决定

1. 删除 `conf/smp-uart.conf` 和 `boards/smp-uart.overlay`，移除 `--smp-uart`。
   `tools/build_mcuboot.py` 只保留默认 IAPv3 和 `--no-upload` 两种产出。
2. IAPv3 是唯一镜像上传接收器，因此不再存在两个写 slot1 的接收端，互斥约束
   由“二选一”变为“只有一个”，Kconfig 的 `!MCUMGR_GRP_IMG` 依赖保留作防御。
3. USART0 固定为 Shell/日志口，不再为 SMP 关闭串口 Shell 后端。
4. MCUboot 校验、交换、回退和设备树分区布局不变；`boot_write_img_confirmed()`
   保留在应用启动路径。
5. 历史验证记录保留原时点含义，不回写为当前能力；不得据此重新引入 SMP 构建。

## 结果

默认构建的 Flash/RAM 不变。删除的是可选的第二通道，产品升级路径未改变。
已烧录 SMP 固件的存量设备不在本仓库的升级覆盖范围内：其固件不含 IAP 接收器，
只能继续使用 SMP 或通过 SWD 重刷；本次交付未包含此类迁移。

mcumgr 代码仍属 Zephyr 上游，恢复时需要重新引入配置、chosen 绑定和写端仲裁，
不属于简单回滚。
