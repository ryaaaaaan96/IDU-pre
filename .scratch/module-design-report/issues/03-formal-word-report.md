Status: resolved
Blocked by: 02

# 调整为正式报告并导出 Word

## 需求与实现

用户要求去除“设计思路：为什么这样划分”等讲解式标题，采用报告形式和 Word 文档模式。

- HTML 调整为白底 A4 文档样式，去除卡片导航、交互按钮和折叠。
- 正文采用封面、目录、项目概述、总体架构、模块设计、产品集成、验证和后续计划结构。
- 模块设计理由改为“设计方案”下的正式段落；保留 API 表和调用示例。
- 17 份完整头文件声明移至附录 A，减少正文中断。
- 生成 docs/module-design-report.docx，含标题层级、表格、代码、页眉与页码。
- scripts/docs/export_module_report.py 使用标准库生成 OOXML；后续可从 HTML 重新导出。
- 文档导航提供 Word 和 HTML 双入口。

## 验证

- DOCX ZIP 完整性与全部 XML 解析通过；A4 页面配置、标题章节、12 张表格检查通过。
- 9 个调用示例和 17 份声明共 26 块代码逐行核对保留。
- HTML 标签结构、本地文件链接、目录锚点检查通过；指定讲解式标题已移除。
- ruff format / check 与 git diff --check 通过。
- 环境没有 Word、LibreOffice 或浏览器，未进行实际分页和视觉渲染验证。
- 不修改固件源码，不重新运行固件测试。
