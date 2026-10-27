# 移除 SMP 上传入口：验收记录

状态：2026-10-27，桌面验证完成，未实板验证。

## 变更

- 删除 `conf/smp-uart.conf`、`boards/smp-uart.overlay`。
- `tools/build_mcuboot.py` 与 `scripts/build.sh` 移除 `--smp-uart`，只保留
  默认 IAPv3 和 `--no-upload`。
- `app/Kconfig` 的 `IDU_IAPV3` 帮助文本改为“唯一上传接收器”；
  `!MCUMGR_GRP_IMG` 依赖保留作防御。
- `tests/test_mcuboot_image.py` 去掉 SMP 元组；
  `tests/test_mcuboot_artifacts.py` 默认目录改为 `-iapv3`。
- 文档同步：构建与升级、验证说明、硬件、编辑器、WSL/J-Link、文档导航、
  根 README、AGENTS 约定、`modules/iap/README.md`。
- 新增 [ADR 0013](../../docs/adr/0013-iapv3-only-upload.md)。

## 验证

| 项 | 结果 |
| --- | --- |
| `tests/test_mcuboot_image.py` | 6 项通过 |
| `tests/test_build_configuration.py` | 8 项通过 |
| 默认 IAPv3 构建 | 应用 Flash 153,696 B / RAM 120,060 B；MCUboot 36,836 B / 18,096 B；无警告 |
| `--no-upload` 构建 | 应用 Flash 132,120 B / RAM 91,204 B；无警告 |
| `test_mcuboot_artifacts.py` | IAPv3 与 no-upload 两个导出目录均通过，含 5 项拒绝用例 |
| SGZ 导出 | 154,816 B，AES 传输格式 |
| `git diff --check` | 通过 |

命令（工作区根目录）：

```bash
./applications/IDU-pre/scripts/build.sh
ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
  GNUARMEMB_TOOLCHAIN_PATH=<arm-none-eabi> \
  ./applications/IDU-pre/scripts/build.sh --no-upload --pristine always
python3 applications/IDU-pre/tests/test_mcuboot_artifacts.py build/hex/IDU-pre-mcuboot-iapv3
```

`build/IDU-pre/mcuboot` 残留旧布局缓存，引用已删除的
`boards/mcuboot-boot.overlay`；本次用 `--pristine always` 重建，未改依赖。

日志位于工作区 `build/IDU-pre/remove-smp-iapv3.log`、
`build/IDU-pre/remove-smp-noupload.log`。

## 未验证

- 实板 IAPv3 上传、SGZ 解密/验签时序。
- USART0 Shell 在移除 SMP 后的实际交互（本次只确认构建配置不含 mcumgr）。
- 存量 SMP 设备迁移，本次未涉及。
