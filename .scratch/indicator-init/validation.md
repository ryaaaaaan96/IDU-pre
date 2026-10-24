# 指示灯初始化失败立即终止启动

Status: resolved

2026-10-24，用户要求 GPIO 未就绪或配置失败后直接返回故障，不继续启动，并将
初始化中的三元表达式及三元返回展开为显式错误分支。

## 实现

- GPIO 未就绪时报告 `STATUS_FAULT_LED` / `STATUS_STARTING` / `-ENODEV` 并返回。
- GPIO 配置失败时报告同一来源和启动阶段，保留配置错误码并返回。
- 仅在配置成功后设置 `indicator_ready = true`，随后返回 0。
- 启动阶段的 LED 故障不接受 CONTINUE；此选择升级为 RESET，调试配置仍按既有约定
  转为 HOLD。显式 HOLD 仍可留驻，但主启动流程返回错误，不创建后续业务。
- 运行期 LED 输出错误仍按原可选策略处理。初始化未成功时，故障工作不访问该引脚。
- 更新 Kconfig 帮助、Doxygen 接口说明和故障策略文档。

## 验证

| 验证 | 结果 |
| --- | --- |
| acquisition native 构建及 run.py | 36 个独立进程场景通过 |
| `tests/runtime/run.sh` | 24 个真实装配场景通过 |
| `CCACHE_DISABLE=1 ./scripts/build.sh` | Arm MCUboot + IAPv3 构建、验签和导出通过 |
| diff 空白检查 | 通过 |

运行期继续配置同时验证：LED 运行中输出失败允许继续采样；同配置下 LED 初始化
失败必须进入复位，并确认采样次数及后续灯引脚写入次数均为零。其他配置继续验证
初始化失败的复位／留驻／调试行为；未就绪场景也覆盖初始化直接返回及无 GPIO 访问。

native 设置 `ASAN_OPTIONS=detect_leaks=0`，不作为泄漏验证；未跑全仓回归或烧录实板。
日志位于工作区 `build/IDU-pre/indicator-init/`。

用户已有的 `status_init()` 锁位置调整保留为未提交修改，本轮仅提交上述初始化失败
及故障策略改动。构建与测试使用包含该用户改动的当前工作区。
