# models 模块说明

## 1. 模块定位

`models` 模块中的类均为**数据类**，用于在系统各层之间进行数据封装与传递。

这些类本身不负责复杂业务逻辑，不承担文件读写、SQL 字符串切分、语法分析执行等职责，主要用于：

- 封装底层存储结构数据
- 封装 SQL 解析阶段的 AST 数据
- 封装执行阶段的上下文与返回结果
- 封装网络通信过程中使用的数据对象

## 2. 模块目录

当前 `models` 模块目录如下：

- `tokenizer/`
  - 词法分析相关数据类
  - 当前包含：`Token`
- `parser/`
  - 语法分析相关数据类与 AST 节点类
  - 当前包含：`SQLStatement`、`CreateDbStmt`、`CreateTableStmt`、`InsertStmt`、`SelectStmt`、`ConditionNode`
- `executor/`
  - 执行引擎阶段使用的数据类
  - 当前包含：`ExecutionContext`、`ExecutionResult`
- `network/`
  - 网络通信相关数据类
  - 当前包含：`NetData`
- `storage/`
  - 底层存储结构映射数据类
  - 当前包含：`DatabaseBlock`、`TableBlock`、`FieldBlock`、`IndexBlock`、`IntegrityBlock`、`DateTime`

## 3. 设计原则

- 数据类只负责保存数据，不承担复杂业务逻辑
- 各模块之间通过数据类进行标准化的数据传递
- 后续若新增 SQL 语句类型或执行结果类型，应优先在 `models` 中补充对应数据结构
