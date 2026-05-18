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
| `dbVersionMap` | `std::map<std::string, std::uint64_t>` | 全量数据库版本号映射表 |

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
| `dbVersion` | `std::uint64_t` | 该数据库的当前版本号 |

示例：

```json
{
  "name": "school",
  "dbVersion": 3,
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
SQL_TEMP_EXEC_REQUEST
SQL_TEMP_EXEC_RESPONSE
DIRECTORY_REQUEST
DIRECTORY_RESPONSE
DB_VERSION_REQUEST
DB_VERSION_RESPONSE
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
| `dbVersionMap` | 包含当前目标数据库版本的单条映射 `{"dbName": version}` |

示例（查询）：

```json
{
  "type": "SQL_EXEC_REQUEST",
  "id": "1001",
  "dbName": "school",
  "sql": "SELECT * FROM student;",
  "dbVersionMap": {
    "school": 3
  }
}
```

示例（非查询）：

```json
{
  "type": "SQL_EXEC_REQUEST",
  "id": "1001",
  "dbName": "school",
  "sql": "INSERT INTO student VALUES (1, 'Tom', 18);",
  "dbVersionMap": {
    "school": 3
  }
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
| `dbName` | 影响的数据库名 |
| `dbVersionMap` | 服务端全量数据库版本号映射表 |

成功示例：

```json
{
  "type": "SQL_EXEC_RESPONSE",
  "success": true,
  "message": "Execute success.",
  "affectedRows": 1,
  "dbName": "school",
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
}
```

失败示例：

```json
{
  "type": "SQL_EXEC_RESPONSE",
  "success": false,
  "message": "Database version mismatch: client=3, server=4. Please refresh the directory.",
  "dbName": "school",
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
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
| `dbName` | 查询所在的数据库名 |
| `dbVersionMap` | 服务端全量数据库版本号映射表 |

示例：

```json
{
  "type": "SQL_QUERY_RESPONSE",
  "success": true,
  "message": "Query success.",
  "columns": ["id", "name", "age"],
  "rows": [
    ["1", "Tom", "18"],
    ["2", "Jerry", "19"]
  ],
  "dbName": "school",
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
}
```

---

### 5.10 SQL_TEMP_EXEC_REQUEST

客户端发送临时 SQL 执行请求（不改变当前会话的数据库上下文）。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `SQL_TEMP_EXEC_REQUEST` |
| `id` | 用户 id |
| `dbName` | 目标数据库名（必填） |
| `sql` | SQL 语句 |
| `dbVersionMap` | 包含目标数据库版本的单条映射 `{"dbName": version}` |

示例：

```json
{
  "type": "SQL_TEMP_EXEC_REQUEST",
  "id": "1001",
  "dbName": "school",
  "sql": "SELECT * FROM student;",
  "dbVersionMap": {
    "school": 3
  }
}
```

---

### 5.11 SQL_TEMP_EXEC_RESPONSE

服务端返回临时 SQL 执行结果。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `SQL_TEMP_EXEC_RESPONSE` |
| `success` | 是否执行成功 |
| `message` | 执行结果提示或错误信息 |
| `columns` | 查询结果列名列表 |
| `rows` | 查询结果二维数据 |
| `affectedRows` | 非查询 SQL 影响的行数 |
| `dbName` | 目标数据库名 |
| `dbVersionMap` | 服务端全量数据库版本号映射表 |

示例：

```json
{
  "type": "SQL_TEMP_EXEC_RESPONSE",
  "success": true,
  "columns": ["id", "name", "age"],
  "rows": [["1", "Tom", "18"]],
  "dbName": "school",
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
}
```

---

### 5.12 DIRECTORY_REQUEST

客户端请求数据库目录。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `DIRECTORY_REQUEST` |
| `id` | 用户 id，可选 |

示例：

```json
{
  "type": "DIRECTORY_REQUEST",
  "id": "1001"
}
```

---

### 5.13 DIRECTORY_RESPONSE

服务端返回数据库目录。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `DIRECTORY_RESPONSE` |
| `success` | 获取目录是否成功 |
| `message` | 提示信息 |
| `databases` | 数据库目录三级结构 |
| `dbVersionMap` | 服务端全量数据库版本号映射表 |

示例：

```json
{
  "type": "DIRECTORY_RESPONSE",
  "success": true,
  "message": "Directory loaded.",
  "databases": [
    {
      "name": "school",
      "dbVersion": 4,
      "tables": [
        {
          "name": "student",
          "fields": ["id", "name", "age"]
        }
      ]
    }
  ],
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
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
```

---

### 5.14 DB_VERSION_REQUEST

客户端请求获取所有数据库的当前版本号。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `DB_VERSION_REQUEST` |
| `id` | 用户 id |

---

### 5.15 DB_VERSION_RESPONSE

服务端返回所有数据库的版本号。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `DB_VERSION_RESPONSE` |
| `success` | 是否成功 |
| `message` | 提示信息 |
| `databases` | 数据库版本列表（每个 DatabaseNode 包含 name 和 dbVersion） |
| `dbVersionMap` | 服务端全量数据库版本号映射表 |

示例：

```json
{
  "type": "DB_VERSION_RESPONSE",
  "success": true,
  "databases": [
    {"name": "school", "dbVersion": 4},
    {"name": "system", "dbVersion": 0}
  ],
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
}
```

---

### 5.16 ERROR_RESPONSE

服务端返回通用错误信息。

| 字段 | 说明 |
|---|---|
| `type` | 固定为 `ERROR_RESPONSE` |
| `success` | 固定为 `false` |
| `message` | 错误信息 |
| `dbName` | 可选，错误的数据库上下文 |
| `dbVersionMap` | 可选，服务端全量数据库版本号映射表（版本冲突时返回） |

示例：

```json
{
  "type": "ERROR_RESPONSE",
  "success": false,
  "message": "Database version mismatch.",
  "dbName": "school",
  "dbVersionMap": {
    "school": 4,
    "system": 0
  }
}
```

---

## 6. 数据库版本号核验机制

### 6.1 核验流程

1. 客户端在本地维护一份数据库版本号缓存（`DirectoryWidget::dbVersionMap`）
2. DDL/DML 请求发送时，客户端将目标数据库的版本号放入 `dbVersionMap` 字段发送给服务端
3. 服务端收到请求后，从 `dbVersionMap` 中提取该数据库的版本号，与本地存储（`.ver` 文件）中的版本号对比
4. 若版本号匹配，则执行请求；若不匹配（且非双方均为 0），则返回 `success=false` 并附带服务端当前全量版本映射表
5. 客户端收到版本不匹配响应后，自动刷新本地版本缓存并提示用户刷新目录

### 6.2 版本号管理

- 每个数据库的版本号存储在 `<dataRoot>/<dbName>/<dbName>.ver` 文件中
- 初始未创建时版本号为 0
- 每次 DDL（CREATE、DROP、ALTER）或 DML（INSERT、UPDATE、DELETE）操作成功后自动递增版本号
- SELECT、SHOW 等查询操作不递增版本号

### 6.3 服务端请求核验流程

```
客户端请求（含 dbVersionMap） → 服务端提取客户端版本号
    ↓
对比服务端本地版本号
    ↓
匹配 → 执行 SQL → DDL/DML 成功后递增版本号 → 全量 dbVersionMap 返回
    ↓
不匹配（且非首次初始状态） → 返回错误 + 服务端全量 dbVersionMap
    ↓
首次初始状态（双方版本号为 0） → 允许执行
```

### 6.4 异常场景处理

| 场景 | 处理方式 |
|---|---|
| 客户端版本 < 服务端版本 | 拒绝执行，返回全量版本映射，客户端刷新缓存 |
| 客户端版本 > 服务端版本 | 拒绝执行（理论不应发生），返回全量版本映射 |
| 双方版本均为 0（新数据库） | 允许执行（首次请求通融） |
| 请求中未携带 dbVersionMap | 进行版本核验，从 map 中查找不到该 db 时客户端版本视为 0 |

---

## 7. 使用示例

### 7.1 构造 SQL 执行请求（含版本号）

```cpp
NetworkTransferData data;
data.setType(NetworkTransferData::SQL_EXEC_REQUEST);
data.setId("1001");
data.setDbName("school");
data.setSql("INSERT INTO student VALUES (1, 'Tom', 18);");

// 携带当前数据库版本号
std::map<std::string, std::uint64_t> vm;
vm["school"] = 3;
data.setDbVersionMap(vm);

std::string jsonStr = data.toJson();
```

### 7.2 解析 SQL 执行响应（含版本映射表）

```cpp
NetworkTransferData data = NetworkTransferData::fromJson(jsonStr);

if (data.getType() == NetworkTransferData::SQL_EXEC_RESPONSE) {
    // 获取全量版本映射表
    const auto &versionMap = data.getDbVersionMap();

    if (data.getSuccess()) {
        int affectedRows = data.getAffectedRows();
        // 更新本地版本缓存
        for (const auto &entry : versionMap) {
            directoryWidget->setDbVersion(
                QString::fromStdString(entry.first), entry.second);
        }
    } else {
        // 版本不匹配等错误
        const std::string &errMsg = data.getMessage();
    }
}
```

### 7.3 解析目录响应

```cpp
NetworkTransferData data = NetworkTransferData::fromJson(jsonStr);

const auto &databases = data.getDatabases();
for (const auto &db : databases) {
    const std::string &dbName = db.getName();
    std::uint64_t version = db.getDbVersion();  // 目录中的版本号
    const auto &tables = db.getTables();
    // ...
}

// 也可通过 dbVersionMap 获取全量版本
const auto &versionMap = data.getDbVersionMap();
```

---

## 8. 注意事项

1. `type` 字符串必须使用类中定义的常量，不建议手写字符串。
2. 所有 SQL 语句（包括查询和非查询）统一使用 `SQL_EXEC_REQUEST` 类型发送，服务端会自动根据语句类型返回对应格式的响应。
3. 查询结果统一使用字符串保存，即使原始数据是数字，也可以转为字符串后放入 `rows`。
4. 该类只负责存储和 JSON 转换，不负责 SQL 执行和 UI 显示。
5. `DIRECTORY_RESPONSE` 中的目录结构为数据库、表、字段三级结构。
6. `dbVersionMap` 字段是全量映射表，服务端在每个响应中都返回完整的版本号映射，客户端维护本地缓存。
7. `DatabaseNode` 中的 `dbVersion` 是单个数据库的版本号，与 `dbVersionMap` 中的信息冗余但提供更便捷的单数据库版本访问。
