Status: resolved
Blocked by: None

# 明确终止故障边界

## Answer

已同步 watchdog 公开头、模块 README、status 公开头和故障策略文档，说明不可恢复、人工/自动重启及可恢复业务异常的职责边界。明确 CONTINUE 仍保留旧行为，不属于产品终止故障约定；未改枚举数值或运行逻辑。

验证：人工核对动作含义及现有接口，git diff --check 通过。仅注释和文档修改，未重新运行构建或实板验证。
