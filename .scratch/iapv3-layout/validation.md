# IAPv3 布局下沉与暂存收敛：验收记录

状态：2026-10-27，桌面验证完成，未实板验证。

## 变更

- `boards/iapv3.overlay` 删除；`iap_journal`、`iap_staging` 及两个 chosen
  移入 `boards/idu/idu_pre/idu_pre.dts`。
- `iap_staging` 由 0xF0000 改为 0x80000，`iap_journal` 保持 0x4000。
- `conf/iapv3.conf` 增加 `IAP_MAX_CONTAINER_SIZE=524288` 与
  `HEAP_MEM_POOL_ADD_SIZE_IAP=12288`。
- `tools/build_mcuboot.py` 不再追加 IAP overlay。
- 文档同步：构建与升级、内存管理、模块接口、AGENTS 约定。

## 验证

| 项 | 结果 |
| --- | --- |
| 默认 IAPv3 构建（`--pristine always`） | 通过，无编译警告 |
| 应用镜像 | Flash 106,100 B / RAM 78,676 B（当前工作树已改为本地 process 装配，数值不可与上一版直接对比） |
| MCUboot | Flash 36,868 B / RAM 18,096 B |
| 系统堆符号 `kheap__system_heap` | 45,404 B，比调整前同配置少 4,096 B |
| 设备树 | `chosen` 指向板级 `iap_staging`/`iap_journal`，暂存 `0x201000 0x80000` |
| SGZ 导出 | 106,960 B，远低于 512 KiB 容器上限 |
| `test_mcuboot_artifacts.py` | 通过（签名、HEX/工厂镜像范围、5 项拒绝用例） |
| `test_mcuboot_image.py` | 6 项通过 |
| `test_build_configuration.py` | 8 项通过 |
| `tests/iap/run.sh` | 通过，2 项跨进程探针按设计跳过 |

命令：

```bash
ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
  GNUARMEMB_TOOLCHAIN_PATH=<arm-none-eabi> \
  ./applications/IDU-pre/scripts/build.sh --pristine always
python3 applications/IDU-pre/tests/test_mcuboot_artifacts.py build/hex/IDU-pre-mcuboot-iapv3
bash applications/IDU-pre/tests/iap/run.sh
```

日志：`build/IDU-pre/iap-layout-build.log`、`build/IDU-pre/iap-layout-tests.log`。

## 未验证

- 实板 IAPv3 上传、解包与验签时序。
- 旧上位机实际容器大小；本次只确认导出 SGZ 远低于上限。
- 升级并发时的堆峰值（当前预算余量约 2 KiB，未做压力测量）。
