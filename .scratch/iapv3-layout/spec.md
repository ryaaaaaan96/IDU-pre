# IAPv3 布局下沉与暂存收敛

状态：2026-10-27，板级布局与容量调整完成，桌面验证通过；实板未验证。

## 目标

1. 删除 `boards/iapv3.overlay`，把 IAP 暂存/日志分区和 chosen 收进板级 DTS，
   使外部 Flash 布局只有一个来源，构建脚本不再拼 overlay。
2. 收敛 SGZ 暂存容量，降低接收器工作区和共享堆预算。

## 事实基线

| 项 | 调整前 | 约束 |
| --- | --- | --- |
| `iap_staging` | 0x201000 / 0xF0000（960 KiB） | 不得与 slot1/scratch 重叠 |
| `IAP_MAX_CONTAINER_SIZE` | 983040 | 必须 ≥ 暂存分区尺寸（`receiver.c`） |
| `HEAP_MEM_POOL_ADD_SIZE_IAP` | 16384 | 覆盖 `receiver_buffers` |
| 镜像上限 | 0x6e000（440 KiB） | 槽 448 KiB 减去尾部预留 |

`receiver_buffers` 组成：512 B 写入缓冲、4.25 KiB 解包工作区、1 KiB 报文、
容器位图（容器/16 包，每包 1 bit）和扇区位图。

## 决定

- 分区与 chosen 移入 `boards/idu/idu_pre/idu_pre.dts`，删除 overlay 与构建分支。
- 暂存改 0x80000（512 KiB），`IAP_MAX_CONTAINER_SIZE=524288`，
  `HEAP_MEM_POOL_ADD_SIZE_IAP=12288`。
- **journal 保持 16 KiB**：缩到 8 KiB 只省 Flash、不省 RAM，且会让既有 NVS
  内容与新分区尺寸失配，需要一次性擦除。
- 模块默认值（`IAP_MAX_CONTAINER_SIZE` 缺省 983040）不修改，产品值在
  `conf/iapv3.conf` 显式覆盖，保持“产品配置显式”的约定。

计算依据：容器是签名镜像加分包开销，0x6e000 的镜像即使按未压缩帧膨胀也在
512 KiB 以内；位图从 7,680 B 降到 4,096 B，`receiver_buffers` 从约 13.6 KiB
降到约 10.0 KiB，堆预算随之下调 4 KiB。

## 验收条件

- 默认构建通过，设备树确认 chosen 指向新分区。
- `test_mcuboot_artifacts.py`、`test_mcuboot_image.py`、
  `test_build_configuration.py` 通过。
- `tests/iap` 独立套件通过。
- 仓库内不再引用 `boards/iapv3.overlay` 和 960 KiB。

## 风险

- 上位机若发送超过 512 KiB 的容器会被拒绝；当前工具产物远小于该值，仍需实测确认。
- 已部署设备：暂存缩小不移动 slot1/scratch，但旧容器若超过 512 KiB 将失败。
