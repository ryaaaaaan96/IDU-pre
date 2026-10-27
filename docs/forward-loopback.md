# 同线转发回环测试

脚本：[run.py](../scripts/forward-loopback/run.py)，配置：[default.json](../scripts/forward-loopback/default.json)。

电脑使用一个 USB–RS485 适配器连接 IDU 的 USART2 总线，脚本同时扮演南向从站 1/2 和北向主站。不要同时运行其他串口主站或模拟从站；不要接地址重复的真实南向设备。适配器必须自动控制收发方向且关闭本地回显。串口不是 shell 口。

在工作区根目录执行：

```bash
source .venv/bin/activate
python -m pip install pyserial
python applications/IDU-pre/scripts/forward-loopback/run.py \
  --port /dev/ttyUSB0 --output build/forward-loopback
```

Windows 使用 `py -m pip install pyserial`，然后 `py applications/IDU-pre/scripts/forward-loopback/run.py --port COM5 --output build/forward-loopback`。可以直接复制整个 `scripts/forward-loopback/` 目录到 Windows 运行。

脚本运行后复位 IDU，默认第一阶段留出 90 秒覆盖当前启动等待和首次发现。全流程约四分钟。默认北向站号 16、115200/8N1，南向 1/2；地址都是实际 PDU 地址。退出码 0 为全部通过，1 为测试失败；串口打开失败等运行错误也会以非零退出。

## 默认检查项目

| 项目 | 验收证据 |
| --- | --- |
| 首次上线及 DI 周期采集 | 在观察窗口收到站号 1、FC04/8152 请求并应答 |
| AI 周期采集 | 收到 FC04/8163 请求并应答 |
| 采集数据到北向缓存 | FC04/10000 的 DI、10001–10002 的 AI 与模拟值一致 |
| 单字参数下发 | 北向 FC06/31500 成功，观察南向写 40000，模拟寄存器等于 321 |
| 单字参数缓存 | 北向 FC03/31500 返回 321 |
| 双字有符号参数 | 北向 FC16/31501 写 -1234（低字在前），南向 40001–40002 及北向缓存一致 |
| 离线探测 | 关闭站号 1 应答，等待掉线，再收到 FC04/8150 初始化探测 |
| 离线时下发失败 | 北向返回异常 0B，期间未出现南向写请求 |
| 恢复上线 | 恢复应答后重新收到 DI 周期采集 |
| 上线后重新设置 | 下发新的 323，南向寄存器及北向缓存均核对成功 |

在线状态通过总线行为推断，没有直接读取固件内部 online 枚举。模拟器会处理初始化 FC04/8150、FC03/30459、周期 FC04/8152/8163/8164，以及参数 FC03/06/16。持久设置所需的读旧值、写应答和读回均由模拟器处理；脚本不访问设备 Flash，不能据此证明断电持久化。

## 修改配置

使用 `--config <文件>` 指定 JSON。`slaves` 的 `holding` 和 `input` 为每个南向站号的寄存器字典；所有字值为 0–65535。`online` 控制初始是否应答。修改这些值时同步修改对应 `read` 步骤期望值。

`steps` 按顺序执行，可添加：

- `online`：`unit`、`value`，切换某从站是否应答。
- `wait`：`seconds`，继续模拟从站但暂不发北向请求。
- `poll`：`unit`、`fc`、`address`、`seconds`、`min_count`，在窗口内检查请求次数。
- `read`：`fc`（3/4）、`address`、`values`，核对北向返回字数组。
- `write`：`fc`（6/16）、`address`、`values`；可添加 `south_unit`（默认 1）、`south_address`、`south_values`，核对实际南向下发和寄存器结果。
- 读写步骤可用 `exception` 指定预期 Modbus 异常码；`no_south_write: true` 检查该窗口未出现南向写。

每步都有 `name`。`stop_on_failure` 默认 true，避免前置失败后继续得到误导结果。持久目标可能由上次运行保存，同值写可能不下发；重复执行时请修改成功写测试的值及后续读取期望值，确保它与固件当前已保存目标不同。

`timeout_s` 为北向超时；`idle_guard_ms` 是发送前总线空闲保护；`reply_delay_ms` 为南向应答延迟；`frame_gap_ms` 为电脑侧判定帧结束的静默时间（默认 4 ms，必须小于最短请求间隔）。电脑串口驱动若把多帧合并交付，脚本会记录 CRC 错误，该轮结果无效，需检查适配器/驱动延迟。当前实现按静默切帧，不承诺解决任意系统调度延迟。

同线没有多主仲裁，空闲保护仍不能排除 IDU 恰好同时发起新请求；偶发超时必须结合原始帧与固件日志判断，不能直接视为转发逻辑错误。

## 输出与验证边界

输出目录生成 `frames.jsonl`（带时间的 TX/RX 原始帧）和 `results.json`（各步骤通过/失败及原因），每次执行会覆盖，保留历史时使用不同 `--output`。脚本退出后模拟设备也停止应答。

已完成的桌面验证为 6 项：已知 CRC、读旧值/写入/读回、双字低字序、离线和恢复、非法多字写不部分更新、非法数量异常。执行命令：

```bash
python applications/IDU-pre/scripts/forward-loopback/test_loopback.py
```

尚未进行实板运行；默认集不覆盖重启/断电恢复、NVS 失败、回读不一致、CRC 注错、广播、并发压力、任意透传或精确掉线时限。后续可在此清单上追加用例。
