# Storage Full Chain Test Report

## 测试结果总览

- **Overall Result**: PASS (13/13)
- **Test Scope**: CREATE DATABASE / CREATE TABLE / INSERT 完整链路
- **Report Time**: 2026-05-07

---

## 测试用例明细

| # | 测试项 | 结果 | 说明 |
|---|--------|------|------|
| 1 | CREATE DATABASE (basic) | ✅ PASS | 成功创建数据库，目录和 `.db` 文件正确生成 |
| 2 | CREATE DATABASE (duplicate) | ✅ PASS | 重复创建被正确拒绝 |
| 3 | USE DATABASE | ✅ PASS | 数据库切换成功 |
| 4 | CREATE TABLE (PK+NOT NULL+DEFAULT) | ✅ PASS | 含约束的表创建成功，`.tdf`/`.trd` 文件正确生成 |
| 5 | CREATE TABLE (duplicate table) | ✅ PASS | 重复创建被正确拒绝 |
| 6 | CREATE TABLE (no database) | ✅ PASS | 未选库时拒绝创建表 |
| 7 | INSERT (full columns) | ✅ PASS | `INSERT INTO t VALUES (...)` 成功 |
| 8 | INSERT (partial columns) | ✅ PASS | `INSERT INTO t (c1, c2) VALUES (...)` 成功 |
| 9 | INSERT (duplicate primary key) | ✅ PASS | 主键重复被拒绝 |
| 10 | INSERT (column count mismatch) | ✅ PASS | 列数不匹配被拒绝 |
| 11 | INSERT (non-existent table) | ✅ PASS | 表不存在被拒绝 |
| 12 | INSERT (no database selected) | ✅ PASS | 未选库时拒绝插入 |
| 13 | INSERT (all columns specified) | ✅ PASS | 显式全列插入成功 |

---

## 本次修复的问题

| 问题 | 文件 | 说明 |
|------|------|------|
| Parser 不支持 `INSERT INTO t VALUES (...)` | `Parser.cpp` | 修改 `parseInsertStatement`，列名为可选 `()` |
| Table 创建使用通用列名而非实际列名 | `CreateTableExecutor.cpp` | 改用 `createTable(dbName, tableName, columnNames)` 传递真实列名 |
| `cleanupDatabaseArtifacts` 不清理 catalog | `StorageFullChainTest.cpp` | 增加 `database.db` 目录清理 |

---

## 功能实现状态总览

### 已完整实现

| 模块 | 功能 | 状态 |
|------|------|------|
| Tokenizer | 全部 SQL 关键字分词 | ✅ |
| Parser | CREATE DATABASE | ✅ |
| Parser | CREATE TABLE (含 PRIMARY KEY / NOT NULL / UNIQUE / DEFAULT 语法) | ✅ |
| Parser | INSERT (含/不含列名) | ✅ |
| Parser | SELECT (基础语法 + WHERE) | ✅ |
| Parser | USE / USE DATABASE | ✅ |
| Parser | SHOW DATABASES / TABLES / TABLE | ✅ |
| Parser | DROP DATABASE / TABLE | ✅ |
| Parser | DELETE (基础语法 + WHERE) | ✅ |
| Parser | UPDATE (基础语法 + WHERE) | ✅ |
| Executor | CREATE DATABASE | ✅ |
| Executor | CREATE TABLE | ✅ |
| Executor | INSERT | ✅ |
| Executor | USE DATABASE / USE | ✅ (含库存在校验) |
| Storage | BTree 索引 | ✅ |
| Storage | 表数据持久化 (.tdf / .trd / .tic / .tid) | ✅ |
| Storage | 数据库目录管理 (catalog) | ✅ |
| Network | TCP 通信 + 长度前缀协议 | ✅ |
| Network | SQL_EXEC_RESPONSE / SQL_QUERY_RESPONSE 分发 | ✅ |

### 语法支持但执行未实现（Stub）

| 功能 | 状态 | 说明 |
|------|------|------|
| **SELECT (真实数据查询)** | ❌ Stub | `SelectExecutor` 仅支持 `DATABASE`/`TABLE` 伪表，`WHERE` 条件未调用实际存储层 |
| **SHOW** | ❌ Stub | `ShowExecutor` 返回固定桩消息 |
| **DROP DATABASE / TABLE** | ❌ Stub | `DropExecutor` 返回固定桩消息 |
| **DELETE** | ❌ Stub | `DeleteExecutor` 返回固定桩消息 |
| **UPDATE** | ❌ Stub | `UpdateExecutor` 返回固定桩消息 |

### 语法支持但约束未执行

| 约束 | 状态 | 说明 |
|------|------|------|
| **NOT NULL** | ❌ 未校验 | Parser 解析正确，Executor 未强制 |
| **DEFAULT** | ❌ 未执行 | Parser 解析正确，插入时未读取默认值 |
| **UNIQUE** | ❌ 未校验 | Parser 解析正确，Executor 未强制 |

### 尚未支持

| 功能 | 状态 | 说明 |
|------|------|------|
| **LOGIN / VERIFY** | ❌ 未实现 | 返回 "not implemented yet" |
| **DIRECTORY_REQUEST** | ❌ 未实现 | 返回 "not implemented yet" |
| **复合主键** | ❌ 不支持 | 仅支持单列主键 |
| **多行 INSERT** | ❌ 不支持 | `INSERT INTO t VALUES (...), (...);` 语法不支持 |
| **列类型校验** | ❌ 未实现 | INSERT 时不验证值与列类型匹配 |
| **ALTER TABLE** | ❌ 未实现 | 无语法与执行支持 |
| **JOIN / 子查询** | ❌ 未实现 | SELECT 不支持复杂查询 |
