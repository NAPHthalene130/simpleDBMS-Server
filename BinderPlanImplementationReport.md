# Binder 与 Plan 模块实现报告（更新版）

**作者**: NAPH130  
**日期**: 2026-05-14  
**分支**: naph-final

---

## 一、概述

按照 DBMS 项目设计文档 `full.md` 的要求，在服务端 Parser 层与 Executor 层之间新增了 **Binder** 层和 **Plan** 层，重构查询处理链路，实现了以下新功能：

- 多表 JOIN (INNER/LEFT/RIGHT)
- 聚合查询 (COUNT/SUM/AVG/MIN/MAX)
- GROUP BY / HAVING
- ORDER BY / LIMIT
- UNION / UNION ALL
- 子查询 (IN/EXISTS/NOT IN/NOT EXISTS)

### 整体架构

```
SQL Text → Tokenizer → Parser（AST）→ Binder（BoundStatement）→ Plan（PlanNode Tree）→ PlanExecutor（Result）
                                                                               ↘ (简单DML) → ExecutorEngine
```

---

## 二、新增模块

### Binder 模块 (`src/binder/`)
| 文件 | 职责 |
|------|------|
| `Binder.h/.cpp` | 语义绑定：表名/列名绑定、星号展开、聚合识别 |
| `BinderManager.h/.cpp` | 模块管理器 |
| `src/models/binder/BindResult.h` | 绑定结果数据结构 |

### Plan 模块 (`src/plan/`)
| 文件 | 职责 |
|------|------|
| `PlanNode.h` | PlanNode 类型定义 (SeqScan/Filter/Projection/NestedLoopJoin/Aggregation) |
| `Planner.h/.cpp` | 将 BindResult 转为 PlanNode 树 |
| `PlanExecutor.h/.cpp` | 递归执行 PlanNode 树，含 ORDER BY/LIMIT/HAVING/子查询处理 |
| `PlanManager.h/.cpp` | 模块管理器 |

---

## 三、功能实现清单

### 已实现功能

| 功能 | 状态 | 测试 |
|------|------|------|
| INNER JOIN | ✅ | `SELECT * FROM a INNER JOIN b ON a.id=b.id` → 3行 |
| LEFT JOIN | ✅ | `SELECT * FROM a LEFT JOIN b ON ...` → 4行 |
| RIGHT JOIN | ✅ | `SELECT * FROM a RIGHT JOIN b ON ...` → 4行 |
| COUNT(*) | ✅ | 值=4 |
| COUNT(col) | ✅ | 值=4 |
| SUM | ✅ | 值=26000.0 |
| AVG | ✅ | 值=6500.0 |
| MIN | ✅ | 值=5000 |
| MAX | ✅ | 值=8000 |
| GROUP BY + 聚合 | ✅ | `SELECT dept_id, COUNT(*) ... GROUP BY dept_id` → 3组 |
| JOIN + GROUP BY | ✅ | `SELECT dept_name, COUNT(*) FROM e JOIN d ON ... GROUP BY dept_name` → 2组 |
| ORDER BY ASC/DESC | ✅ | id升序/降序正确 |
| LIMIT | ✅ | LIMIT 2 返回2行 |
| UNION | ✅ | 去重后3行 |
| UNION ALL | ✅ | 不去重5行 |
| HAVING (列) | ✅ | `HAVING val > 15` → 2行 |
| HAVING (聚合) | ✅ | `HAVING COUNT(*) >= 1` → 3行 |
| JOIN + HAVING | ✅ | `JOIN + GROUP BY + HAVING COUNT(*) >= 1` → 2行 |
| 子查询 IN | ✅ | `WHERE id IN (SELECT id FROM t2)` → 2行 |
| table.column 谓词 | ✅ | `WHERE a.col = b.col` 解析支持 |

### 未实现功能

| 功能 | 说明 |
|------|------|
| 子查询 WHERE col = (SELECT...) | 标量子查询部分支持 |
| 子查询中 JOIN | 仅支持简单单表子查询 |
| EXISTS/NOT EXISTS | 解析支持，执行逻辑已预留 |
| 索引扫描 (IndexScan) | PlanNode 已规划，索引接口已有 |
| 复杂嵌套子查询 | 暂未实现 |

---

## 四、测试结果

### 测试1: BinderPlanJoinAggTest (17/17 PASS)
| 测试项 | 结果 |
|--------|------|
| INNER JOIN | PASS |
| LEFT JOIN | PASS |
| RIGHT JOIN | PASS |
| COUNT(*)/COUNT(id) | PASS |
| SUM/AVG/MIN/MAX | PASS |
| GROUP BY + COUNT | PASS |
| JOIN + GROUP BY + COUNT | PASS |

### 测试2: OrderLimitUnionHavingSubqTest (20/20 PASS)
| 测试项 | 结果 |
|--------|------|
| ORDER BY ASC/DESC | PASS |
| ORDER BY val ASC | PASS |
| LIMIT 2 | PASS |
| ORDER BY DESC LIMIT 1 | PASS |
| UNION | PASS |
| UNION ALL | PASS |
| HAVING COUNT>=1 | PASS |
| HAVING val>15 | PASS |
| JOIN + HAVING | PASS |
| WHERE IN subquery | PASS |

**总计: 37/37 测试通过**

---

## 五、提交记录

| 提交 | 摘要 |
|------|------|
| `feat(binder): 新增 Binder 和 Plan 模块...` | Binder+Plan 模块创建、Core 集成 |
| `fix(parser): 修复 JOIN 解析...` | Parser 修复 + 测试 |
| `feat(parser): 实现 ORDER BY 和 LIMIT` | ORDER BY/LIMIT |
| `feat(parser): 实现 UNION` | UNION/UNION ALL |
| `feat(executor): 实现多表 HAVING` | HAVING 过滤 |
| `feat(parser): 实现子查询支持` | IN/EXISTS 子查询 |
| `fix(executor): 修复单表投影...` | 单表投影 + 综合测试 |

## 六、关键设计决策

1. **低耦合**：Binder/Plan 均通过 Manager 模式管理，模块间通过数据结构传递
2. **渐进集成**：DML 语句仍走 ExecutorEngine，仅 SELECT 走 Binder+Plan
3. **双路径执行**：JOIN 走 `selectJoinRows()`；单表走 `Table::select()` 后聚合
4. **Storage 非侵入**：仅新增 RIGHT_JOIN 类型
