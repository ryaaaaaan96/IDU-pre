# 移除 SMP 上传入口

状态：2026-10-27，用户确认删除 SMP 上传配置并同步修改；实现与桌面验收完成，实板未验证。

## 目标

删除 `--smp-uart` 构建选项、`conf/smp-uart.conf`、`boards/smp-uart.overlay`
及相关测试与文档引用，使 IAPv3 成为唯一镜像上传接收器，USART0 恢复为普通
Shell/日志口。

## 事实基线

| 项 | 已核实事实 |
| --- | --- |
| 应用引用 | 应用与模块源码没有任何 mcumgr 引用；`app/main.c:39` 的 `boot_write_img_confirmed()` 属于 MCUboot img manager，两种构建共用 |
| 互斥原因 | `modules/iap/Kconfig` 与 `app/Kconfig` 都用 `!MCUMGR_GRP_IMG` 排除第二写端，两者都写 slot1 |
| 物理口 | SMP 走 USART0（`smp-uart.overlay` 的 chosen），IAPv3 走 USART2/RS485，不共用线路 |
| 容量代价 | 历史记录 SMP 比 IAPv3 多约 5 KB RAM、13 KB Flash（`docs/migration-history.md`） |
| 分区归属 | `iapv3.overlay` 的两个 IAP 分区与 SMP 无关，本次保留 |

## 决定

1. 删除 `conf/smp-uart.conf`、`boards/smp-uart.overlay`，移除 `--smp-uart`。
2. `tools/build_mcuboot.py` 只保留默认 IAPv3 与 `--no-upload`；`--iapv3` 仍可显式指定。
3. Kconfig 的 `!MCUMGR_GRP_IMG` 依赖保留，作为“不引入第二写端”的防御约束。
4. 历史验证章节保留原时点含义，不回写；当前能力以本文件和文档正文为准。
5. 决策记录见 [ADR 0013](../../docs/adr/0013-iapv3-only-upload.md)。

## 验收条件

- 仓库内不再有 `--smp-uart`、`mcuboot-smp`、`smp-uart.conf` 的有效引用（历史章节除外）。
- `tests/test_mcuboot_image.py` 与 `tests/test_build_configuration.py` 通过。
- 默认 IAPv3 与 `--no-upload` 构建、导出、验签通过；默认构建的产物目录和分区不变。
- 未做实物验证，不据此宣称任何实板升级行为。

## 风险

已烧录 SMP 固件的存量设备固件不含 IAP 接收器，删除构建入口后只能继续使用 SMP
或通过 SWD 重刷。当前无此类设备记录；如存在需单独规划迁移。
