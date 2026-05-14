# Binder 与 Plan 模块实现报告

**作者**: NAPH130  
**日期**: 2026-05-14  
**分支**: naph-final

---

## 一、概述

按照 DBMS 项目设计文档 `full.md` 的要求，在服务端 Parser 层与 Executor 层之间新增了 **Binder（语义绑定）** 层和 **Plan（执行计划）** 层，重构了查询处理链路，并实现了多表 JOIN 查询与聚合查询的后端功能。

### 整体架构（重构后）

```
SQL Text → Tokenizer → Parser（AST）→ Binder（BoundStatement）→ Plan（PlanNode Tree）→ Executor（Result）
```

---

## 二、新增模块详情

### 2.1 Binder 模块 (`src/binder/`)

| 文件 | 职责 |
|------|------|
| `Binder.h/.cpp` | 语义绑定核心类：表名绑定、列名绑定、星号展开、聚合函数识别、类型校验 |
| `BinderManager.h/.cpp` | 模块管理器，统一管理 Binder 生命周期 |
| `src/models/binder/BindResult.h` | 绑定结果数据结构（BoundTableRef、BoundJoinRef、AggregateExpr 等） |

**Binder 核心功能**：
- **表绑定**：将 Parser 输出的表名字符串解析为实际 `storage::TableSchema`
- **列绑定**：校验目标列是否存在于已绑定表中，支持 `alias.column` 形式
- **星号展开**：将 `SELECT *` 展开为显式列名列表（多表时加表别名前缀）
- **聚合函数识别**：正则匹配 `COUNT(*)`、`SUM(col)`、`AVG(col)`、`MIN(col)`、`MAX(col)`
- **JOIN 绑定**：绑定 JOIN 子句中的表名并校验别名冲突

### 2.2 Plan 模块 (`src/plan/`)

| 文件 | 职责 |
|------|------|
| `PlanNode.h` | 计划节点类型定义（SeqScan、Filter、Projection、NestedLoopJoin、Aggregation 等） |
| `Planner.h/.cpp` | 计划生成器：将 BindResult 转换为 PlanNode 树 |
| `PlanExecutor.h/.cpp` | 计划树执行器：递归遍历 PlanNode 树并执行 |
| `PlanManager.h/.cpp` | 模块管理器，统一管理 Plan 层生命周期 |

**PlanNode 类型**：
| 节点类型 | 用途 | 关键字段 |
|----------|------|----------|
| `SeqScanPlanNode` | 全表扫描 | dbName, tableName, tableAlias |
| `FilterPlanNode` | WHERE 条件过滤 | condition (ConditionNode) |
| `ProjectionPlanNode` | 列投影 | projectedColumns |
| `NestedLoopJoinPlanNode` | 嵌套循环连接 | joinType, leftAlias, rightAlias, onCondition, rightSchema |
| `AggregationPlanNode` | 聚合计算 | aggregateExprs, groupByColumns, havingCondition |

### 2.3 已有模块扩展

| 文件 | 变更内容 |
|------|----------|
| `SelectStmt.h/.cpp` | 新增 `JoinInfo` 结构体及 `addJoinInfo()`、`hasJoin()` 方法 |
| `Parser.h/.cpp` | 新增 `parseJoinClauses()`（解析 INNER/LEFT/RIGHT JOIN）、`parseSelectTargetList()`（支持聚合函数列表）、`parsePredicate()` 支持 `table.column` 点分标识符 |
| `Tokenizer.cpp` | 新增 JOIN/LEFT/RIGHT/INNER/ON/GROUP/BY/HAVING 等关键字 |
| `DatabaseManager.h/.cpp` | JoinType 新增 `RIGHT_JOIN`，selectJoinRows 实现 RIGHT_JOIN 语义 |
| `Core.h/.cpp` | 集成 BinderManager 和 PlanManager |
| `NetReceiver.cpp` | SELECT 语句走 Binder → Plan → PlanExecutor 新管道 |

---

## 三、功能实现清单

### 已实现功能 ✓

| 功能 | 状态 | 说明 |
|------|------|------|
| **INNER JOIN** | ✅ 已实现 | `SELECT * FROM a INNER JOIN b ON a.id = b.id` |
| **LEFT JOIN** | ✅ 已实现 | `SELECT * FROM a LEFT JOIN b ON a.id = b.id` |
| **RIGHT JOIN** | ✅ 已实现 | `SELECT * FROM a RIGHT JOIN b ON a.id = b.id` |
| **COUNT(*)** | ✅ 已实现 | `SELECT COUNT(*) FROM table` |
| **COUNT(column)** | ✅ 已实现 | `SELECT COUNT(col) FROM table` |
| **SUM** | ✅ 已实现 | `SELECT SUM(col) FROM table` |
| **AVG** | ✅ 已实现 | `SELECT AVG(col) FROM table` |
| **MIN** | ✅ 已实现 | `SELECT MIN(col) FROM table` |
| **MAX** | ✅ 已实现 | `SELECT MAX(col) FROM table` |
| **GROUP BY + 聚合** | ✅ 已实现 | `SELECT col, COUNT(*) FROM t GROUP BY col` |
| **JOIN + GROUP BY** | ✅ 已实现 | `SELECT dept_name, COUNT(*) FROM e JOIN d ON ... GROUP BY dept_name` |
| **表别名（解析层）** | ✅ 已实现 | Parser 支持 `AS alias` 和隐式别名语法 |
| **table.column 谓词** | ✅ 已实现 | `WHERE a.col = b.col` 解析支持 |

### 未实现/待实现功能 ✗

| 功能 | 状态 | 说明 |
|------|------|------|
| **ORDER BY** | ✗ 未实现 | Parser 已注册关键字，解析逻辑待补充 |
| **LIMIT** | ✗ 未实现 | Parser 已注册关键字，解析逻辑待补充 |
| **多表 HAVING** | ✗ 部分支持 | 单表 HAVING 已支持，JOIN 场景的 HAVING 过滤待完善 |
| **子查询** | ✗ 未实现 | Table 引擎层已有 SubquerySpec 结构，Plan 层集成待完成 |
| **索引扫描** | ✗ 未实现 | PlanNode 已规划 IndexScanPlan 类型，B+ 树索引已有但未接入 Plan |
| **INSERT SELECT** | ✗ 未实现 | 仅支持 VALUES 形式的 INSERT |
| **UNION** | ✗ 未实现 | 整个链路均未支持 |
| **CROSS JOIN** | ✗ 未实现 | 未注册 CROSS 关键字 |
| **SELF JOIN** | ⚠️ 受限 | 需通过别名实现，Binder 别名冲突校验可能阻止自连接 |
| **WHERE 子句中带表前缀** | ⚠️ 部分支持 | Parser 支持 `a.col = b.val`，但 WHERE 执行的列解析为单表场景 |

---

## 四、测试结果

**测试方式**：模拟从前端传入 `NetworkTransferData`，通过 `NetReceiver` 全链路处理 SQL 请求。

**测试文件**：`src/test/BinderPlanJoinAggTest.cpp`  
**测试报告**：`src/test/BinderPlanJoinAggTestReport.md`

### 测试通过率：17/17 = 100%

| 测试项 | 结果 |
|--------|------|
| CREATE DATABASE | PASS |
| USE DATABASE | PASS |
| CREATE TABLE employees | PASS |
| CREATE TABLE departments | PASS |
| INSERT employees (4行) | PASS |
| INSERT departments (3行) | PASS |
| INNER JOIN (3行) | PASS |
| LEFT JOIN (4行) | PASS |
| RIGHT JOIN (4行) | PASS |
| COUNT(*) = 4 | PASS |
| COUNT(id) = 4 | PASS |
| SUM(salary) = 26000 | PASS |
| AVG(salary) = 6500 | PASS |
| MIN(salary) = 5000 | PASS |
| MAX(salary) = 8000 | PASS |
| GROUP BY dept_id + COUNT (3组) | PASS |
| JOIN + GROUP BY dept_name + COUNT (2组) | PASS |

---

## 五、关键设计决策

1. **低耦合设计**：Binder 和 Plan 均通过 XxxxManager 模式管理，对 Core 通过指针注入依赖，模块间通过 BindResult 和 PlanNode 等数据结构传递信息。

2. **渐进式集成**：非 SELECT 语句（INSERT/UPDATE/DELETE/CREATE 等）仍走原有 ExecutorEngine 路径，仅 SELECT 语句启用 Binder+Plan 管道。

3. **PlanExecutor 双路径**：JOIN 查询走 `DatabaseManager::selectJoinRows()` 复用已有 JOIN 引擎；单表查询走 `Table::select()` 后再做聚合。

4. **聚合计算**：自已实现聚合逻辑（分组、COUNT/SUM/AVG/MIN/MAX），利用 `std::map` 进行分组。

5. **Storage 层非侵入**：仅对 `DatabaseManager::JoinType` 新增 `RIGHT_JOIN`，其余 Storage 层接口未变更。
