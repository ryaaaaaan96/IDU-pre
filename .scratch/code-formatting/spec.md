# 统一代码排版

按用户要求展开压缩的代码，限制长行。C/H 为 4 空格、100 列、展开条件/循环/函数/
case，长参数换行；Python 使用 100 列。配置存于根目录 .clang-format、.ruff.toml。
范围为 app、boards、自有 modules、tests 的 C/H 和工具/测试 Python；保留运行逻辑，
不修改 west 依赖、生成产物及用户的 forward-loopback 文件。
