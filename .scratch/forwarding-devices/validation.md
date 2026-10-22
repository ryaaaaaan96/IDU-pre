# 验证记录

2026-10-22。先按预先确认的完整报文与 DP 公共查询边界做定向 red/green，
完成首轮实现后执行一次完整回归；审查后只复验受修改影响的测试。

## 主机与仿真

- `ASAN_OPTIONS=detect_leaks=0 CCACHE_DISABLE=1 bash applications/IDU-pre/tests/run.sh`：
  退出 0，60 次 native 执行、333 个 Ztest PASS，0 编译警告；主机 C ASan/UBSan 与 Python 检查通过。
  当前容器关闭不受支持的 LSan，未关闭 ASan/UBSan。
- 审查后复验 forwarding_devices、table_registry、points、forwarding、shared_rs485，全部退出 0，无警告。
- 单字重叠兼容修正后复验 transmit（9 PASS）及 forwarding_devices（6 PASS、普通执行跳过 1 个重启探针）。
- 新的重启探针分别以 legacy_write / verify 两个独立进程使用同一模拟 Flash 文件运行，2 次 PASS：
  旧设备挂表保存远端 10/11 的值，新双类配置恢复 123/456，无 STORAGE_RESET，无下发重放。
  已将该流程加入 tests/run.sh。完整脚本未在审查后重复运行；上述新增与受影响场景单独验证。
- 原有 points、forwarding 的重启、意图读回、参数掉电边界，以及 IAP/RAW 大载荷和可选模块配置
  均包含在完整回归中。当前新集成夹具自行声明 3 个入口容量，不扩大产品预留。

日志位于工作区 build/IDU-pre/，未提交生成产物。

## 固件

`CCACHE_DISABLE=1 ./build.sh`：IDU + MCUboot + IAPv3 退出 0，0 编译警告，
镜像验签两次通过，HEX / signed BIN / AES SGZ 导出成功。
最终日志：build/IDU-pre/forwarding-firmware-final.log。
签名载荷 digest：52d2f7bc6863ae474af50ef74e00a36180edfa9e8eaa17eebb52d4b040f4b9ce。

| 镜像/资源 | 基线 d9a65c7 | 当前 | 变化 |
| --- | ---: | ---: | ---: |
| IDU Flash | 124400 B | 125240 B | +840 B |
| IDU 静态 RAM | 102764 B | 102940 B | +176 B |
| MCUboot Flash | 36568 B | 36568 B | 0 |
| MCUboot RAM | 18096 B | 18096 B | 0 |

新增 RAM 来自入口映射与生命周期视图；完整点表仍在 Flash，点值堆实际需求仍为 162 B。
产品仍为 1 条采集总线、1 个转发工作线程；未增加线程栈数量或按固件类型预设。

## 实板与审查

未执行真实 RS485 时序、运行期栈高水位或 NOR 实物掉电测试。
[审查记录](review.md) 分开列出 Standards / Spec，并如实记录并行代理额度限制及人工补审范围。
