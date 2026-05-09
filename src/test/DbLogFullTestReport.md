# DbLog 完整测试报告

- **Overall**: PASS（预期）
- **测试文件**: `src/test/DbLogFullTest.cpp`
- **分支**: `NAPH130-Log`
- **作者**: NAPH130

## 测试概述

本测试模拟客户端通过 `NetReceiver` 网络层传入 SQL 语句，覆盖 DDL/DML 全流程操作，
同时验证 `DbLogManager` 日志记录模块的正确性与完整性。测试端口独立使用 **19087**。

---

## 测试步骤

### 阶段一：DDL 操作

| 步骤 | 名称 | 操作 | 验证点 |
|------|------|------|--------|
| 1 | CREATE DATABASE | `CREATE DATABASE test_dblog_db` | 响应成功，文件系统目录创建 |
| 2 | USE DATABASE | `USE DATABASE test_dblog_db` | 切换数据库成功 |
| 3 | CREATE TABLE | 含 id PK, name NOT NULL, age DEFAULT 18, dept | `.tdf` / `.trd` 文件存在 |
| 4 | SHOW DATABASES | 列出所有数据库 | 返回行数 > 0 |
| 5 | SHOW TABLES | 列出当前库所有表 | 返回行数 > 0 |

### 阶段二：DML 操作

| 步骤 | 名称 | 操作 | 验证点 |
|------|------|------|--------|
| 6 | INSERT 完整列 | `(1, 'Alice', 25, 'Engineering')` | 响应成功 |
| 7 | INSERT 多行 | `(2, 'Bob', 30, 'Marketing')` | 响应成功 |
| 8 | INSERT 部分列 | `(id, name, dept) VALUES (3, 'Carol', 'Sales')` | 默认 age=18 |
| 9 | INSERT NOT NULL | `(id, dept)` 不填 name | 预期失败 |
| 10 | INSERT 重复 PK | `(1, 'Dup', 99, 'Test')` | 预期失败 |
| 11 | SELECT * | 全表查询 | 返回 3 行 |
| 12 | SELECT WHERE | `WHERE age > 20` | 返回 >= 2 行 |
| 13 | UPDATE | `SET age = 26 WHERE id = 1` | affectedRows = 1 |
| 14 | 验证 UPDATE | 查询 id=1 的 age | age = "26" |
| 15 | DELETE | `WHERE id = 3` | affectedRows = 1 |
| 16 | 验证 DELETE | SELECT * 全表 | 剩余 2 行 |

### 阶段三：DDL 删除

| 步骤 | 名称 | 操作 | 验证点 |
|------|------|------|--------|
| 17 | DROP TABLE | `DROP TABLE employees` | 响应成功 |
| 18 | SHOW TABLES | 确认空表 | 返回 0 行 |
| 19 | DROP DATABASE | `DROP DATABASE test_dblog_db` | 响应成功 |
| 20 | SHOW DATABASES | 确认库已移除 | TEST_DB 不存在 |

### 阶段四：DbLog 验证

| 步骤 | 名称 | 验证点 |
|------|------|--------|
| 21 | 实例存在 | `core.getDbLogManager() != nullptr` |
| 22 | 日志文件存在 | `src/dbLog/dbOperation.log` 存在 |
| 23 | 操作计数 | `getCurrentOperationId() >= 8` |
| 24 | 按库查询 | `getLogsForDatabase(...).size() >= 8` |
| 25 | CreateDatabase 日志 | 包含对应操作类型 |
| 26 | CreateTable 日志 | 包含对应操作类型 |
| 27 | Insert 日志 | 包含对应操作类型 |
| 28 | Update 日志 | 包含对应操作类型 |
| 29 | Delete 日志 | 包含对应操作类型 |
| 30 | DropTable 日志 | 包含对应操作类型 |
| 31 | DropDatabase 日志 | 包含对应操作类型 |
| 32 | 日志排序 | 按时间升序 |
| 33 | dbRecover | 调用成功返回 true |
| 34 | JSON 往返 | `toJsonString` → `fromJsonString` 一致 |
| 35 | 空库查询 | 不存在库返回空列表 |

---

## 覆盖摘要

| 类别 | 操作 | 覆盖 |
|------|------|:--:|
| DDL | CREATE DATABASE | ✓ |
| DDL | DROP DATABASE | ✓ |
| DDL | CREATE TABLE | ✓ |
| DDL | DROP TABLE | ✓ |
| DML | INSERT | ✓ |
| DML | SELECT | ✓ |
| DML | UPDATE | ✓ |
| DML | DELETE | ✓ |
| 边界 | NOT NULL 违规 | ✓ |
| 边界 | 重复主键 | ✓ |
| 边界 | INSERT 默认值 | ✓ |
| 查询 | SHOW DATABASES | ✓ |
| 查询 | SHOW TABLES | ✓ |
| DbLog | 日志文件持久化 | ✓ |
| DbLog | 7 种操作类型记录 | ✓ |
| DbLog | dbRecover 恢复 | ✓ |
| DbLog | JSON 序列化 | ✓ |

---

## 构建与运行

```bash
# CMake 自动发现测试文件
cmake --build build/vs2022-x64 --config Debug --target simpleDBMS-Server-DbLogFullTest

# 运行
./build/vs2022-x64/Debug/simpleDBMS-Server-DbLogFullTest.exe
```

测试运行后自动生成 `DbLogFullTestReport.md`，控制台输出 35 步 PASS/FAIL 明细。
