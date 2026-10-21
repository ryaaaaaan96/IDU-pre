# 任务管理：本地 Markdown 与 Git

所有路径均相对于 `applications/IDU-pre/`，内容使用中文，
代码标识符和技能约定的状态字段保留原文。

## 文件约定

- 每项功能使用 `.scratch/<feature>/` 目录，按需创建。
- 规格保存在 `spec.md`。
- 任务分别保存为 `issues/<NN>-<slug>.md`，从 `01` 开始编号；
  一个任务一个文件，不合并为单一任务清单。
- 文件顶部使用 `Status:` 记录状态。
- 使用 `Blocked by:` 记录前置任务编号；无依赖写 `None`。
- 讨论记录追加在文件末尾的 `## Comments` 下。
- 任务正文记录目标、验收条件和验证结果。

## 技能操作

- “发布到任务系统”：在上述目录创建或更新 Markdown 文件。
- “读取相关任务”：读取指定文件；编号存在歧义时先确认所属功能。
- 任务文档纳入本地 Git 管理。
- 完成一轮修改并验证后，只提交当前任务相关文件。
- 不自动创建远端 Issue，不自动推送。

## 探索任务

需要探索路线图时，使用 `.scratch/<feature>/map.md` 记录问题、
决定与待确认事项，每个子任务仍单独保存。

- `Type:` 可使用 `research`、`prototype`、`grilling`、`task`。
- 开始处理时设置 `Status: claimed`。
- 依赖全部完成后，任务才能开始。
- 完成时追加 `## Answer`，设置 `Status: resolved`，
  并在 `map.md` 中记录结论和任务链接。
