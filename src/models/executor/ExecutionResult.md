# `ExecutionResult` 接口说明

## 1. 文件定位

- 头文件：`src/models/executor/ExecutionResult.h`
- 实现文件：`src/models/executor/ExecutionResult.cpp`
- 所属模块：`models/executor`

`ExecutionResult` 是执行层返回给上层调用方的统一结果数据类，用于描述一条 SQL 语句在执行后的状态、提示信息、影响行数、结果集内容，以及当前结果关联的数据库名和表名。

---

## 2. 相关类型

### `ExecutionStatus`

定义如下：

```cpp
enum class ExecutionStatus
{
    Success,
    Failure
};
```

含义如下：

- `ExecutionStatus::Success`
  - 表示 SQL 执行成功。
- `ExecutionStatus::Failure`
  - 表示 SQL 执行失败，或执行流程在某个阶段被提前中止。

---

## 3. 类职责

`ExecutionResult` 只负责保存执行结果数据，不负责执行 SQL，也不负责网络传输或存储写入逻辑。

它通常由以下模块创建或返回：

- `ExecutorEngine`
- 各具体语句执行器，例如：
  - `CreateDbExecutor`
  - `CreateTableExecutor`
  - `InsertExecutor`
  - `SelectExecutor`

---

## 4. 成员字段说明

### `status`

类型：

```cpp
ExecutionStatus
```

作用：

- 表示当前执行结果的整体状态。

典型取值：

- `Success`：执行成功
- `Failure`：执行失败

### `message`

类型：

```cpp
std::string
```

作用：

- 保存执行结果的补充说明。
- 成功时可用于保存成功提示。
- 失败时通常保存错误原因。

当前代码中的典型失败消息包括：

- `"Invalid execute input pointer."`
- `"Unsupported SQL statement type."`
- `"Executor exception: ..."`
- `"Unknown executor exception."`
- `"CreateDbExecutor received null input pointer."`
- `"CreateDbExecutor is registered, but execution logic is not implemented yet."`
- `"Insert statement columns and values do not match."`

### `affectedRows`

类型：

```cpp
std::int32_t
```

作用：

- 表示本次 SQL 执行影响的记录数。

典型语义：

- `0`
  - 没有影响任何记录。
  - 或该语句类型本身不以“影响行数”为主要返回信息。
  - 或当前执行失败。
- 正整数
  - 成功影响了对应数量的记录。

### `resultSet`

类型：

```cpp
std::vector<std::vector<std::string>>
```

作用：

- 保存查询类语句返回的二维表结果。
- 外层 `vector` 表示多行。
- 内层 `vector` 表示一行中的多个字段值。

典型语义：

- 空数组
  - 非查询语句，例如 `CREATE`、`INSERT`
  - 查询结果为空
  - 执行失败
- 非空二维数组
  - 查询语句成功，并返回了结果数据

### `dbName`

类型：

```cpp
std::string
```

作用：

- 表示当前执行结果关联的数据库名称。
- 便于网络层、客户端或日志系统识别该结果属于哪个数据库上下文。

典型语义：

- 空字符串
  - 当前请求没有明确数据库上下文
  - 语句执行前就因空指针等问题提前失败
- 非空字符串
  - 执行结果明确绑定到某个数据库
  - 例如当前会话选中的数据库，或 `CREATE DATABASE` 语句中的目标数据库

### `tableName`

类型：

```cpp
std::string
```

作用：

- 表示当前执行结果关联的数据表名称。
- 便于客户端在收到结果后进一步刷新目录树、数据表视图或日志信息。

典型语义：

- 空字符串
  - 当前语句不涉及具体数据表
  - 例如数据库级操作或执行前校验失败
- 非空字符串
  - 当前结果与某个数据表直接相关
  - 例如 `CREATE TABLE`、`INSERT`、`SELECT ... FROM tableName`

---

## 5. 接口说明

### `ExecutionResult::ExecutionResult()`

声明：

```cpp
ExecutionResult();
```

当前实现：

```cpp
ExecutionResult::ExecutionResult()
    : status(ExecutionStatus::Failure), affectedRows(0)
{
}
```

说明：

- 默认构造出的对象是一个“失败态”的结果对象。
- `status` 默认值为 `ExecutionStatus::Failure`
- `affectedRows` 默认值为 `0`
- `message` 默认是空字符串
- `resultSet` 默认是空数组
- `dbName` 默认是空字符串
- `tableName` 默认是空字符串

设计意义：

- 调用方即使忘记显式设置状态，也不会误把一个未初始化结果当成成功结果使用。

### `ExecutionStatus getStatus() const`

作用：

- 获取当前执行状态。

返回值说明：

- 返回 `Success` 表示执行成功。
- 返回 `Failure` 表示执行失败。

### `void setStatus(ExecutionStatus status)`

作用：

- 设置执行状态。

使用建议：

- 在执行成功路径中显式设置为 `ExecutionStatus::Success`
- 在执行失败路径中显式设置为 `ExecutionStatus::Failure`

### `const std::string &getMessage() const`

作用：

- 获取说明信息字符串。

返回值说明：

- 失败时通常是错误原因。
- 成功时通常是成功提示，或可读性较好的补充信息。
- 若为空字符串，表示当前没有附加说明。

### `void setMessage(const std::string &message)`

作用：

- 设置说明信息。

使用建议：

- 失败时应尽量填写明确错误原因。
- 成功时建议填写简洁结果摘要，例如：
  - `"Create database succeeded."`
  - `"Inserted 1 row."`
  - `"Select executed successfully."`

### `std::int32_t getAffectedRows() const`

作用：

- 获取影响行数。

返回值说明：

- 返回 `0` 不一定表示失败，也可能表示：
  - 当前语句不是修改型语句
  - 查询结果为空
  - 当前逻辑尚未设置影响行数

### `void setAffectedRows(std::int32_t affectedRows)`

作用：

- 设置影响行数。

使用建议：

- `INSERT`、`UPDATE`、`DELETE` 等写操作成功后应设置为实际受影响行数。
- 对 `CREATE DATABASE`、`CREATE TABLE` 这类 DDL，可按业务需要设置为 `0`。

### `const std::vector<std::vector<std::string>> &getResultSet() const`

作用：

- 获取结果集数据。

返回值说明：

- 返回空结果集可能代表：
  - 非查询语句
  - 查询成功但无数据
  - 执行失败

因此调用方不能只凭 `resultSet.empty()` 判断成功或失败，必须结合 `status` 一起判断。

### `void setResultSet(const std::vector<std::vector<std::string>> &resultSet)`

作用：

- 设置查询结果集。

使用建议：

- 仅查询类语句在成功时设置有意义的数据。
- 若结果集中每一行字段顺序有约定，应由执行器保证顺序一致。

### `const std::string &getDbName() const`

作用：

- 获取当前执行结果关联的数据库名。

返回值说明：

- 为空字符串时表示当前结果未绑定数据库上下文。
- 非空时表示该结果与对应数据库直接相关。

### `void setDbName(const std::string &dbName)`

作用：

- 设置当前执行结果关联的数据库名。

使用建议：

- `CREATE DATABASE` 应设置为目标数据库名。
- 依赖当前数据库上下文的语句，建议设置为 `ExecutionContext` 中的当前数据库名。

### `const std::string &getTableName() const`

作用：

- 获取当前执行结果关联的数据表名。

返回值说明：

- 为空字符串时表示当前结果未绑定具体表。
- 非空时表示该结果与对应表直接相关。

### `void setTableName(const std::string &tableName)`

作用：

- 设置当前执行结果关联的数据表名。

使用建议：

- `CREATE TABLE`、`INSERT`、`SELECT` 等涉及具体表的语句应尽量设置该值。
- 若语句本身不面向具体表，可保留为空字符串。

### `std::string toJson() const`

作用：

- 将当前执行结果对象序列化为 JSON 字符串。
- 便于网络层直接透传到客户端。

当前输出字段包括：

- `status`
- `message`
- `affectedRows`
- `resultSet`
- `dbName`
- `tableName`

序列化示例：

```json
{
  "status": "Success",
  "message": "Create table succeeded.",
  "affectedRows": 0,
  "resultSet": [],
  "dbName": "school",
  "tableName": "student"
}
```

### `static ExecutionResult fromJson(const std::string &jsonStr)`

作用：

- 从 JSON 字符串反序列化出 `ExecutionResult` 对象。

使用建议：

- 主要用于客户端或网络接收端恢复服务端返回的结构化执行结果。
- 当某个字段在 JSON 中缺失时，当前实现会回退到默认值。

---

## 6. 不同情况下的值说明

下面说明的是当前项目中 `ExecutionResult` 各字段的典型含义。

### 情况 1：默认刚构造完成，尚未填值

典型值：

- `status = Failure`
- `message = ""`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName = ""`
- `tableName = ""`

说明：

- 这是一个“未明确成功”的默认结果。
- 不能把这种状态视为成功执行完成。

### 情况 2：执行入口参数非法

例如：

- 传入的 `statement == nullptr`
- 传入的 `executionContext == nullptr`

当前代码中的典型值：

- `status = Failure`
- `message = "Invalid execute input pointer."`
  - 或某个具体执行器中的 `"xxx received null input pointer."`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName = ""`
- `tableName = ""`

### 情况 3：语句类型没有对应执行器

例如：

- `ExecutorEngine` 无法找到对应 `statementType` 的执行器

当前代码中的典型值：

- `status = Failure`
- `message = "Unsupported SQL statement type."`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName =` 通常来自当前执行上下文
- `tableName = ""`

### 情况 4：执行器收到错误语句类型

例如：

- `InsertExecutor` 收到的实际语句不是 `Insert`

当前代码中的典型值：

- `status = Failure`
- `message = "InsertExecutor received mismatched statement type."`
  - 其它执行器会有类似格式
- `affectedRows = 0`
- `resultSet = {}`
- `dbName =` 若上下文可用则为当前数据库名
- `tableName =` 若语句对象可读则为对应表名，否则为空

### 情况 5：执行器内部校验失败

例如：

- `InsertExecutor` 检测到列数量与值数量不匹配
- `SelectExecutor` 检测到目标字段非法

当前代码中的典型值：

- `status = Failure`
- `message` 为具体校验失败原因
- `affectedRows = 0`
- `resultSet = {}`
- `dbName =` 当前数据库名或目标数据库名
- `tableName =` 当前目标表名，若语句不涉及具体表则为空

### 情况 6：执行逻辑尚未实现

这是当前项目执行器最常见的真实状态。

当前代码中的典型值：

- `status = Failure`
- `message = "... execution logic is not implemented yet."`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName =` 当前数据库名
- `tableName =` 当前目标表名

说明：

- 当前 `CreateDbExecutor`、`CreateTableExecutor`、`InsertExecutor`、`SelectExecutor` 都已注册，但主体执行逻辑尚未完成。
- 因此目前大多数合法 SQL 最终也会得到失败结果。

### 情况 7：执行器抛出异常

例如：

- 执行过程中抛出标准异常
- 抛出未知异常

当前代码中的典型值：

- 标准异常：
  - `status = Failure`
  - `message = "Executor exception: " + exception.what()`
  - `affectedRows = 0`
  - `resultSet = {}`
  - `dbName =` 当前数据库名（若上下文可用）
  - `tableName = ""`
- 未知异常：
  - `status = Failure`
  - `message = "Unknown executor exception."`
  - `affectedRows = 0`
  - `resultSet = {}`
  - `dbName =` 当前数据库名（若上下文可用）
  - `tableName = ""`

### 情况 8：未来的非查询语句执行成功

例如：

- `CREATE DATABASE`
- `CREATE TABLE`
- `INSERT`

推荐值约定：

- `status = Success`
- `message =` 简洁成功信息
- `affectedRows = 0` 或实际影响行数
- `resultSet = {}`
- `dbName =` 当前数据库名或目标数据库名
- `tableName =` 涉及表时写入对应表名，否则为空

建议示例：

#### `CREATE DATABASE` 成功

- `status = Success`
- `message = "Create database succeeded."`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName = "school"`
- `tableName = ""`

#### `CREATE TABLE` 成功

- `status = Success`
- `message = "Create table succeeded."`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName = "school"`
- `tableName = "student"`

#### `INSERT` 成功

- `status = Success`
- `message = "Inserted 1 row."`
- `affectedRows = 1`
- `resultSet = {}`
- `dbName = "school"`
- `tableName = "student"`

### 情况 9：未来的查询语句执行成功

例如：

- `SELECT * FROM student;`

推荐值约定：

- `status = Success`
- `message = "Select executed successfully."`
- `affectedRows = 0`
  - 查询语句通常不以影响行数为核心
- `resultSet` 为查询得到的二维数组
- `dbName =` 当前数据库名
- `tableName =` 查询目标表名

建议示例：

```cpp
status = Success
message = "Select executed successfully."
affectedRows = 0
resultSet = {
    {"1", "Alice", "20"},
    {"2", "Bob", "21"}
}
dbName = "school"
tableName = "student"
```

### 情况 10：查询成功但无数据

推荐值约定：

- `status = Success`
- `message = "Select executed successfully, but no rows matched."`
- `affectedRows = 0`
- `resultSet = {}`
- `dbName =` 当前数据库名
- `tableName =` 查询目标表名

说明：

- “空结果集”不等于失败。
- 判断查询是否失败，必须优先看 `status`。

---

## 7. 调用方判断建议

推荐判断顺序如下：
  

1. 先判断 `status`
3. 再根据需要读取 `dbName` 与 `tableName`
4. 最后根据语句类型决定是否读取 `affectedRows` 或 `resultSet`

不推荐的做法：

- 仅凭 `affectedRows == 0` 判断失败
- 仅凭 `resultSet.empty()` 判断失败
- 忽略 `dbName` 和 `tableName`，导致客户端无法识别刷新范围

推荐示意：

```cpp
ExecutionResult result = executorEngine->execute(statement, &executionContext);

if (result.getStatus() == ExecutionStatus::Failure) {
    // 失败时优先读取错误消息
    std::cout << result.getMessage() << std::endl;
    return;
}

// 成功后再按语句类型读取影响行数或结果集
```

---

## 8. 当前实现状态总结

截至当前代码版本，`ExecutionResult` 的使用特点如下：

- 默认构造即失败态
- 失败路径已经较完整
- 已支持 `toJson()` 与 `fromJson()` 进行结构化传输
- 已支持通过 `dbName` 与 `tableName` 传递结果所属对象信息
- 成功路径仍只在部分执行器中落地
- 当前大多数执行结果依然以失败结果返回
- 后续当各执行器补完业务逻辑后，应统一按本文档中的成功语义填充全部字段

