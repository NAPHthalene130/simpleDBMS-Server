# NetworkTransferData 网络传输数据类说明文档

## 1. 类的作用

`NetworkTransferData` 是客户端和服务端之间进行网络通信时使用的统一业务数据类。

客户端发送请求时，可以将 `NetworkTransferData` 对象通过 `toJson()` 转换为 JSON 字符串后发送给服务端。

服务端收到 JSON 字符串后，可以通过 `fromJson()` 还原成 `NetworkTransferData` 对象，并根据 `type` 判断请求类型。

服务端返回结果时也使用同一个数据类，客户端收到后同样通过 `fromJson()` 解析。

该类本质上只是一个统一的数据容器，不包含复杂业务逻辑。

---

## 2. 主要字段说明

| 字段名 | 类型 | 含义 |
|---|---|---|
| `type` | `std::string` | 数据类型，用于区分登录、验证、SQL 请求、目录请求等不同场景 |
| `id` | `std::string` | 用户 id |
| `password` | `std::string` | 用户密码 |
| `dbName` | `std::string` | 当前数据库名或目标数据库名 |
| `sql` | `std::string` | SQL 语句或命令内容 |
| `success` | `bool` | 请求或操作是否成功 |
| `message` | `std::string` | 服务端返回的提示信息或错误信息 |
| `affectedRows` | `int` | 非查询 SQL 影响的行数 |
| `columns` | `std::vector<std::string>` | 查询结果的列名列表 |
| `rows` | `std::vector<std::vector<std::string>>` | 查询结果的二维数据 |
| `databases` | `std::vector<DatabaseNode>` | 数据库目录结构，包含数据库、表、字段三级信息 |

---

## 3. 目录结构相关类

### 3.1 TableNode

`TableNode` 表示一个数据表节点。

| 字段名 | 类型 | 含义 |
|---|---|---|
| `name` | `std::string` | 表名 |
| `fields` | `std::vector<std::string>` | 字段名列表 |

示例：

```json
{
  "name": "student",
  "fields": ["id", "name", "age"]
}
```

### 3.2 DatabaseNode

`DatabaseNode` 表示一个数据库节点。

| 字段名 | 类型 | 含义 |
|---|---|---|
| `name` | `std::string` | 数据库名 |
| `tables` | `std::vector<TableNode>` | 当前数据库下的数据表列表 |

示例：

```json
{
  "name": "school",
  "tables": [
    {
      "name": "student",
      "fields": ["id", "name", "age"]
    },
    {
      "name": "teacher",
      "fields": ["id", "name", "course"]
    }
  ]
}
```

---

## 4. type 类型说明

`NetworkTransferData` 使用 `type` 字段区分不同网络传输场景。

当前定义的 type 包括：

```cpp
LOGIN_REQUEST
LOGIN_RESPONSE
VERIFY_REQUEST
VERIFY_RESPONSE
USE_DATABASE_REQUEST
USE_DATABASE_RESPONSE
SQL_EXEC_REQUEST
SQL_EXEC_RESPONSE
SQL_QUERY_RESPONSE
DIRECTORY_REQUEST
DIRECTORY_RESPONSE
ERROR_RESPONSE
```

---

## 5. 不同 type 下字段使用说明

### 5.1 LOGIN_REQUEST

客户端发送登录请求。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `LOGIN_REQUEST` |
| `id` | 用户 id |
| `password` | 用户密码 |

示例：

```json
{
  "type": "LOGIN_REQUEST",
  "id": "1001",
  "password": "123456"
}
```

---

### 5.2 LOGIN_RESPONSE

服务端返回登录结果。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `LOGIN_RESPONSE` |
| `success` | 登录是否成功 |
| `message` | 登录结果提示信息 |
| `id` | 可选，登录成功时返回用户 id |

示例：

```json
{
  "type": "LOGIN_RESPONSE",
  "id": "1001",
  "success": true,
  "message": "Login success."
}
```

---

### 5.3 VERIFY_REQUEST

客户端发送连接状态验证请求。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `VERIFY_REQUEST` |
| `id` | 用户 id |
| `password` | 用户密码 |

示例：

```json
{
  "type": "VERIFY_REQUEST",
  "id": "1001",
  "password": "123456"
}
```

---

### 5.4 VERIFY_RESPONSE

服务端返回连接状态验证结果。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `VERIFY_RESPONSE` |
| `success` | 验证是否通过 |
| `message` | 验证结果提示信息 |

示例：

```json
{
  "type": "VERIFY_RESPONSE",
  "success": true,
  "message": "Connection verified."
}
```

---

### 5.5 USE_DATABASE_REQUEST

客户端发送 `USE dbName` 命令。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `USE_DATABASE_REQUEST` |
| `id` | 用户 id |
| `dbName` | 目标数据库名 |
| `sql` | 原始 USE 命令，可选 |

示例：

```json
{
  "type": "USE_DATABASE_REQUEST",
  "id": "1001",
  "dbName": "school",
  "sql": "USE school;"
}
```

---

### 5.6 USE_DATABASE_RESPONSE

服务端返回数据库切换结果。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `USE_DATABASE_RESPONSE` |
| `success` | 是否切换成功 |
| `dbName` | 当前数据库名 |
| `message` | 切换结果提示信息 |

示例：

```json
{
  "type": "USE_DATABASE_RESPONSE",
  "success": true,
  "dbName": "school",
  "message": "Database changed to school."
}
```

---

### 5.7 SQL_EXEC_REQUEST

客户端发送 SQL 执行请求，适用于所有 SQL 语句类型（包括查询和非查询）。

适用于：

- `SELECT`、`SHOW` 等查询语句
- `INSERT`、`UPDATE`、`DELETE` 等非查询语句
- `CREATE`、`DROP`、`ALTER` 等 DDL 语句
- 其它所有 SQL 语句

服务端会根据实际执行的语句类型，自动在响应中返回对应的结果格式（查询结果包含 columns/rows，非查询结果包含 affectedRows）。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `SQL_EXEC_REQUEST` |
| `id` | 用户 id |
| `dbName` | 当前数据库名 |
| `sql` | SQL 语句 |

示例（查询）：

```json
{
  "type": "SQL_EXEC_REQUEST",
  "id": "1001",
  "dbName": "school",
  "sql": "SELECT * FROM student;"
}
```

示例（非查询）：

```json
{
  "type": "SQL_EXEC_REQUEST",
  "id": "1001",
  "dbName": "school",
  "sql": "INSERT INTO student VALUES (1, 'Tom', 18);"
}
```

---

### 5.8 SQL_EXEC_RESPONSE

服务端返回非查询 SQL（INSERT、UPDATE、DELETE、CREATE、DROP 等）的执行结果。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `SQL_EXEC_RESPONSE` |
| `success` | SQL 是否执行成功 |
| `message` | 执行结果提示或错误信息 |
| `affectedRows` | 影响行数 |

非查询成功示例：

```json
{
  "type": "SQL_EXEC_RESPONSE",
  "success": true,
  "message": "Execute success.",
  "affectedRows": 1
}
```

非查询失败示例：

```json
{
  "type": "SQL_EXEC_RESPONSE",
  "success": false,
  "message": "Database already exists."
}
```

---

### 5.9 SQL_QUERY_RESPONSE

服务端返回查询 SQL（SELECT、SHOW 等）的执行结果。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `SQL_QUERY_RESPONSE` |
| `success` | 查询是否执行成功 |
| `message` | 执行结果提示或错误信息 |
| `columns` | 查询结果列名列表 |
| `rows` | 查询结果二维数据，每一行为一个 `std::vector<std::string>` |

查询成功示例：

```json
{
  "type": "SQL_QUERY_RESPONSE",
  "success": true,
  "message": "Query success.",
  "columns": ["id", "name", "age"],
  "rows": [
    ["1", "Tom", "18"],
    ["2", "Jerry", "19"]
  ]
}
```

查询失败示例：

```json
{
  "type": "SQL_QUERY_RESPONSE",
  "success": false,
  "message": "Table does not exist."
}
```

---

### 5.10 DIRECTORY_REQUEST

客户端请求数据库目录。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `DIRECTORY_REQUEST` |
| `id` | 用户 id，可选 |
| `dbName` | 当前数据库名，可选 |

示例：

```json
{
  "type": "DIRECTORY_REQUEST",
  "id": "1001"
}
```

---

### 5.11 DIRECTORY_RESPONSE

服务端返回数据库目录。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `DIRECTORY_RESPONSE` |
| `success` | 获取目录是否成功 |
| `message` | 提示信息 |
| `databases` | 数据库目录三级结构 |

示例：

```json
{
  "type": "DIRECTORY_RESPONSE",
  "success": true,
  "message": "Directory loaded.",
  "databases": [
    {
      "name": "school",
      "tables": [
        {
          "name": "student",
          "fields": ["id", "name", "age"]
        },
        {
          "name": "teacher",
          "fields": ["id", "name", "course"]
        }
      ]
    }
  ]
}
```

目录结构说明：

```text
database
  table
    field
```

即：

```text
school
  student
    id
    name
    age
  teacher
    id
    name
    course
```

---

### 5.12 ERROR_RESPONSE

服务端返回通用错误信息。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `ERROR_RESPONSE` |
| `success` | 固定为 `false` |
| `message` | 错误信息 |

示例：

```json
{
  "type": "ERROR_RESPONSE",
  "success": false,
  "message": "Invalid request type."
}
```

---

## 6. 使用示例

### 6.1 构造 SQL 执行请求

```cpp
NetworkTransferData data;
data.setType(NetworkTransferData::SQL_EXEC_REQUEST);
data.setId("1001");
data.setDbName("school");
data.setSql("SELECT * FROM student;");

std::string jsonStr = data.toJson();
```

---

### 6.2 解析 SQL 执行响应

```cpp
NetworkTransferData data = NetworkTransferData::fromJson(jsonStr);

if (data.getType() == NetworkTransferData::SQL_EXEC_RESPONSE && data.getSuccess()) {
    // 查询结果使用 columns 和 rows
    const std::vector<std::string> &columns = data.getColumns();
    const std::vector<std::vector<std::string>> &rows = data.getRows();

    // 非查询结果使用 affectedRows
    int affectedRows = data.getAffectedRows();
}
```

---

## 7. 注意事项

1. `type` 字符串必须使用类中定义的常量，不建议手写字符串。
2. 所有 SQL 语句（包括查询和非查询）统一使用 `SQL_EXEC_REQUEST` 类型发送，服务端会自动根据语句类型返回对应格式的响应。
3. 查询结果统一使用字符串保存，即使原始数据是数字，也可以转为字符串后放入 `rows`。
4. 该类只负责存储和 JSON 转换，不负责 SQL 执行和 UI 显示。
5. `DIRECTORY_RESPONSE` 中的目录结构为数据库、表、字段三级结构。
6. 如果项目中其它位置使用 `userID` 命名，需要说明本类中的 `id` 表示同一含义。
