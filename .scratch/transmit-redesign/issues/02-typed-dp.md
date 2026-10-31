Status: resolved
Blocked by: None
Type: task

# DP 显式类型接口与整数转换

用户授权开始代码修改后，先落地通信模块依赖的 DP 类型接口。
transmit/vport、物理口装配及协议放缩仍未实现，不代表整个通信方案已经完成。

## 实现

- 四个 key/index 同步读写接口增加显式 input_type/output_type，删除无类型调用形式。
- 私有 can_convert 在回调前拒绝非法类型和数组跨类型组合；convert_value 用 int64_t 承接整数，
  两个 switch 分别取源值与保存目标值；跨整数类型先清零输出，缩窄保留低位。
- 有符号目标通过宽有符号算术解释补码，不使用越界有符号强制转换。
- SET 转内部类型再回调、校验、更新；GET 内部回调、校验更新后转输出类型。
- 调用方输入和失败的读取输出保持不变，不增加锁、常驻类型缓存或元信息查询。
- 迁移应用采样、周期读取及所有当前 C 测试与 ADC 替身。
- NVS 编码与内部类型不变；存储测试使用 U32 输入写 U16 点、S32 输出读回。

## 验证

2026-10-31：

- dp_core：普通及 DP_HASH_LOOKUP 两种模式通过，各 3 个用例；包含 45 组源值/内部类型设置、
  各组 5 种输出类型，以及回调顺序、截断、范围、非法类型和失败输出保护。
- process_runtime：普通及 hash 两种模式通过。
- dp_storage：普通及 hash 两种模式的 70 次跨进程阶段通过，包含恢复、故障注入及并发保存。
- adc：native_sim 回归通过。
- 默认 MCUboot/IAPv3 固件构建及 imgtool 验签通过；没有烧录或实板测试。
- clang-format 检查、git diff --check 和相关文档本地链接检查通过。

验证日志为本地产物：/tmp/typed-*-build.log、/tmp/typed-*-test.log、/tmp/typed-dp-firmware.log。
固件仍导出到工作区 build/hex/IDU-pre-mcuboot-iapv3/。

## 后续

继续按 docs/transmit-design.md 实现通信模块；放缩字段、事件接口、位点映射等尚未收口，
本轮不恢复旧通信框架或 IAP 路由，不宣称串口转发/上传已可用。
