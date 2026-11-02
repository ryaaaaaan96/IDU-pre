# 构建、烧录与升级

[文档导航](README.md) · [硬件](hardware.md) · [验证说明](testing.md)

## 构建

工作区使用 Zephyr v4.5.0、SDK 1.0.1；SDK 已注册到 CMake。
以下命令均在 West 工作区根目录执行，脚本自动使用工作区 .venv：

```bash
./applications/IDU-pre/scripts/build.sh --pristine always
# 明确选择既有密钥
./applications/IDU-pre/scripts/build.sh --key applications/IDU-pre/keys/IDU-pre-dev.pem
# 不包含上传接收器
./applications/IDU-pre/scripts/build.sh --no-upload
```

默认 MCUboot + IAPv3。`--version 0.1.1+1` 设置镜像版本，
`--extra-conf`、`--extra-overlay` 可重复指定额外配置；相对路径基于工作区。
切换版本或工具链后使用 `--pristine always`。Modbus 适配只接受经审查的 4.5.0 源码，
哈希/锚点不匹配时先审查，不能跳过保护。

默认密钥为 build/keys/IDU-pre-dev.pem，首次构建自动生成，后续必须沿用。
清理整个 build 前单独备份密钥；已有 MCUboot 只接受对应签名，私钥不加入 Git。

## 产物

应用构建目录：`build/IDU-pre/mcuboot-iapv3/IDU-pre/`。
默认导出目录：`build/hex/IDU-pre-mcuboot-iapv3/`；无上传时为 `build/hex/IDU-pre-mcuboot/`。

| 文件 | 用途 |
| --- | --- |
| IDU-pre-factory.hex | MCUboot 与签名应用的合并镜像，首次 SWD 安装 |
| IDU-pre-boot.hex / IDU-pre.hex | 分开的启动程序 / 签名应用 |
| IDU-pre-update.bin | 签名 MCUboot 镜像 |
| IDU-pre-update.sgz | IAPv3 AES/SGZ 上传文件 |
| IDU-pre-public.pem / IDU-pre-validation.json | 公钥与产物校验结果 |

导出器调用 imgtool 验签，检查头、向量、容量及 HEX/BIN 一致性。
最新构建结果和资源见[验证说明](testing.md#固件与资源)。

## Flash 布局

内部地址与外部 NOR 偏移分别计算，以 bootloader/partitions.dtsi 和板级 DTS 为准。

| 位置 | 地址/偏移 | 大小 | 内容 |
| --- | --- | --- | --- |
| 内部 | 0x08000000 | 64 KiB | MCUboot |
| 内部 | 0x08010000 | 448 KiB | slot0 |
| 外部 | 0x010000 | 16 KiB | 旧参数保留区，不自动导入 |
| 外部 | 0x018000 | 16 KiB | IAP 日志 |
| 外部 | 0x01C000 | 64 KiB | DP 参数总区域 |
| 外部 | 0x201000 | 512 KiB | SGZ 暂存 |
| 外部 | 0x300000 | 448 KiB | slot1 |
| 外部 | 0x370000 | 8 KiB | scratch |

slot0 包含 512 B MCUboot 头与交换尾部。导出器保留 8 KiB 尾部，签名升级文件不超过 440 KiB。
DP 区含布局、管理 NVS 和默认六个设备槽位。布局不兼容会清空 DP 区，
不影响 IAP 与镜像；真实读取失败不触发格式重建。详情见[DP 契约](../modules/dataprocess/README.md)。

## 烧录与启动

先启动 Windows J-Link GDB Server，再执行：

```bash
./applications/IDU-pre/scripts/flash.sh --host <Windows主机IPv4> --dry-run
./applications/IDU-pre/scripts/flash.sh --host <Windows主机IPv4>
```

此操作完整擦除内部 Flash 并写 factory HEX，不清理外部 NOR。
从旧启动布局首次安装时，还需确认外部 slot1/scratch 没有旧交换数据；
正常升级不得随意清理其中的回退镜像。实际安装步骤见[远程调试](wsl-jlink.md)。

MCUboot 使用 P-256 签名和 swap-scratch；未确认的试运行镜像在后续复位时可回退。
应用完成必要初始化后确认镜像；当前 startup 随后等待 10 秒再进入运行状态。
确认只证明初始化完成，不保证后续运行稳定性。

<a id="iapv3"></a>
## IAPv3 接入与协议

APP 的 forward.c 登记完整 ADU 地址段，transmit 先验证 CRC、站号与帧边界，再调用 iap_route。
普通测点仍经标准功能码检查；IAP 单独校验旧超长 FC16、包长和会话，不把数据塞入 DP。
当前产品所有广播均不响应，包含 IAP；站号范围 1–246。

协议地址为线上零基数值，不减 40001：

| 地址 | 行为 |
| --- | --- |
| 20049 / 20051 | 通信/协议版本 0x03000000；32 位数低字在前，每字大端 |
| 20055 / 20057 / 20059 | 首包延迟 3000 ms、分包间隔、状态超时 480000 ms |
| 20061 / 20062 | 下级设备数 0、属性 3 |
| 20063–20070 / 20071–20078 | 16 字节状态 / 原因；byte 15 为总状态，byte 0 为 ARM 状态 |
| 20083 | 0xAA 登录；0x55 退出；支持登录、保留字、包参数的合并写入 |
| 20088 / 20089 | 每包长度 / 总包数；长度 16–1024，16 字节对齐 |
| 20090–20094 | 包号从 1 开始、旧地址字段、本包长度、数据 |
| 20099–20103 | 首个缺包号、偏移、长度、缺包总数；读取总数为 0 时启动解包 |


接收支持 1 KiB 包、重复一致性校验、乱序补包和按需擦除。ACK 表示任务准入，
处理成功才清除缺包标记；队列忙时静默，主站应查询并补发。

IAP 专用线程完成 AES 解密、SGZ/QuickLZ 解包、CRC、MCUboot 头/向量、SHA256 和 P-256 验签，
成功后写日志、请求试升级并等待 500 ms 重启。单 Cortex-M 镜像，不支持差分、多镜像或下挂升级。
详细配置见[IAP 模块](../modules/iap/README.md)。

默认流程尚未接入 DP 的 prepare_reboot/cancel_reboot 保存门禁，
最近未落盘参数可能丢失。需要保留时，由 APP 先冻结修改并 dp_flush，再提交升级。
参数默认五秒延后保存，普通 set 成功不等同于已保存。

## SGZ 打包与验证

构建自动读取本次 .config 打包；SGZ 采用合法 QuickLZ stored 块，接收器也支持压缩块。
AES 用于旧传输互操作，固件可信性依赖 P-256。若旧上位机自行加密，生成明文容器避免双重加密：

```bash
python3 applications/IDU-pre/tools/pack_sgz.py \
  --bin build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-update.bin \
  --key build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-public.pem \
  --output build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-update-plain.sgz --plain
```

模拟器完整升级已通过，旧上位机实板互通、掉电交换、回退与外部看门狗交接仍待验证。
