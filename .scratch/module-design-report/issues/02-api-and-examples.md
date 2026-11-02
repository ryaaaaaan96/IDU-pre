Status: resolved
Blocked by: 01

# 细化模块设计、对外 API 和调用示例

## 需求与结果

按用户要求扩展既有 HTML，每个模块补充设计思路、API 用途与约束、当前真实声明和调用示例。

- 9 个模块均提供设计理由、API 清单和示例。
- common 转换、DP 登记与读写、vport 事件、transmit 映射可按顺序阅读；
  DP/vport/transmit 三段示例可组合进行语法检查。
- Modbus 独立服务端、watchdog、IAP 路由、QuickLZ 解码和标准 ADC 读取分别演示。
- Modbus transport 扩展标为保留接口，不宣称当前启用南向 collect。
- 显式说明产品适配函数、已有 watchdog 初始化和示例的依赖条件。
- 真实声明折叠展示，增加模块跳转与全部展开/收起；打印自动展开并恢复原状态。

## 验证

- 当前目标固件 compile_commands.json 参数执行 -fsyntax-only，
  开启隐式函数声明错误检查。7 个编译单元通过：common、DP/vport/transmit 组合、
  modbus、watchdog、iap、quicklz、gd32e505。
- 示例仅语法检查，不链接物理适配函数，不代表执行或实板验证。
- HTML 标签闭合、17 个唯一 ID、33 个本地链接/锚点检查通过。
- 17 个折叠块；17 份声明与实际头文件逐份比对一致，9 个示例与检查源码一致。
- Node --check 检查页面脚本通过；git diff --check 通过。
- 未重新运行固件测试；环境无浏览器，未进行渲染与 PDF 打印实测。
