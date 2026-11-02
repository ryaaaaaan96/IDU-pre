Status: resolved
Blocked by: None

# 将 Modbus 构建副本适配到 Zephyr 4.5.0

## 对比结论

- core 新增 modbus_work_submit 和可选专用队列。
- raw 改用 sizeof(data) 校验上限，修正 RAW 头长度上限，并使用新投递入口。
- serial 更新 UART 中断接口和异步关闭，同时改用新投递入口。
- server.c 与 4.4.2 相同，保留既有边界修复。
- public header 仅注释/头文件保护调整，内部头新增工作投递声明。

## 验收

- 哈希和锚点检查继续严格执行；不跳过检查。
- 自有 RAW 同步与未占用 RAW 默认/专用队列分别验证。
- 模块、组合与产品构建结果另行记录，不将模拟器当实板证据。

## 验证结果

- 模块默认队列 6/6、官方专用队列 6/6，通过同步调用和未占用 RAW 的实际队列归属测试。
- transmit_stream 21/21，protocol_port 1/1，通过。
- iap-transmit 指定完整升级用例 1/1；其余 10 项按过滤跳过，本轮未执行。
- 修改上游副本的探针被哈希检查拒绝；最终生成文件与受测生成版本完全相同。
- Zephyr SDK 1.0.1 / ARM GCC 14.3.0 默认 MCUboot + IAPv3 构建、验签与 SGZ 导出成功。
  使用现有 applications/IDU-pre/keys/IDU-pre-dev.pem，未生成或替换签名密钥。
- 应用 Flash 105524 B、RAM 77944 B；MCUboot Flash 37560 B、RAM 18184 B。
  镜像摘要 a64cb96b679c3e6ee23ada2464f0b25f412224bf5384103d997a5304344e8f09。
- ruff format/check、bash -n tests/run.sh、git diff --check 通过。
- 仅修改应用自有模块和文档；保留用户 Word 锁文件，不更新 manifest 或 west 依赖源码。
- 未执行完整回归、旧 Modbus SERIAL 后端实板测试或新固件实板验收。
  DTS vendor prefix 与地址单元警告保留，未在本轮扩展处理。

## Answer

4.5.0 原生服务端继续处理标准功能码。构建副本保留受控同步扩展、请求占用与边界修复，
未绑定自有 RAW 的请求回到官方 modbus_work_submit，支持新增专用队列。
上游已修复的 RAW 缓冲长度上限直接复用，源码哈希与锚点校验不放宽。
日志位于 /tmp/idu-modbus45-*-build.log 和 /tmp/idu-modbus45-*-test.log。
