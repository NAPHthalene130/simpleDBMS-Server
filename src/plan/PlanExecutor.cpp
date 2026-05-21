#include "PlanExecutor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_set>

#include "Core.h"
#include "log/LogWriter.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace {

std::string toUpperString(const std::string &value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return result;
}

ExecutionResult buildFailure(const std::string &msg, const std::string &dbName = "", const std::string &tableName = "") {
    ExecutionResult r;
    r.setStatus(ExecutionStatus::Failure);
    r.setMessage(msg);
    r.setDbName(dbName);
    r.setTableName(tableName);
    return r;
}

ExecutionResult buildSuccess(const std::string &msg,
                             const std::vector<std::vector<std::string>> &resultSet,
                             const std::vector<std::string> &columns,
                             const std::string &dbName = "",
                             const std::string &tableName = "") {
    ExecutionResult r;
    r.setStatus(ExecutionStatus::Success);
    r.setMessage(msg);
    r.setResultSet(resultSet);
    r.setColumns(columns);
    r.setDbName(dbName);
    r.setTableName(tableName);
    r.setAffectedRows(static_cast<std::int32_t>(resultSet.size()));
    return r;
}

storage::Table::CompareOp mapCompare(const std::string &opStr) {
    if (opStr == "=") return storage::Table::CompareOp::EQ;
    if (opStr == "<>" || opStr == "!=") return storage::Table::CompareOp::NE;
    if (opStr == ">") return storage::Table::CompareOp::GT;
    if (opStr == ">=") return storage::Table::CompareOp::GE;
    if (opStr == "<") return storage::Table::CompareOp::LT;
    if (opStr == "<=") return storage::Table::CompareOp::LE;
    if (opStr == "LIKE") return storage::Table::CompareOp::LIKE;
    return storage::Table::CompareOp::EQ;
}

double parseDoubleSafe(const std::string &s) {
    try {
        std::size_t pos = 0;
        const double v = std::stod(s, &pos);
        return (pos == s.size()) ? v : std::numeric_limits<double>::quiet_NaN();
    } catch (...) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

bool containsSubqueryCondition(const ConditionNode *node) {
    if (node == nullptr) {
        return false;
    }
    if (node->hasSubquery()) {
        return true;
    }
    return containsSubqueryCondition(node->getLeftNode().get())
        || containsSubqueryCondition(node->getRightNode().get());
}

bool tryResolveValueFromRow(const std::string &operand,
                            const std::vector<std::string> &row,
                            const std::vector<std::string> &columns,
                            std::string &outValue) {
    auto exactIt = std::find(columns.begin(), columns.end(), operand);
    if (exactIt != columns.end()) {
        const std::size_t idx = static_cast<std::size_t>(std::distance(columns.begin(), exactIt));
        if (idx < row.size()) {
            outValue = row[idx];
            return true;
        }
    }

    const auto dotPos = operand.find('.');
    const std::string targetCol = dotPos == std::string::npos ? operand : operand.substr(dotPos + 1);
    if (targetCol.empty()) {
        return false;
    }

    std::size_t foundIdx = 0;
    bool found = false;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const std::string &colRef = columns[i];
        std::string colName = colRef;
        const auto colDotPos = colRef.find('.');
        if (colDotPos != std::string::npos) {
            colName = colRef.substr(colDotPos + 1);
        }
        if (colName == targetCol) {
            if (found) {
                return false;
            }
            found = true;
            foundIdx = i;
        }
    }

    if (!found || foundIdx >= row.size()) {
        return false;
    }

    outValue = row[foundIdx];
    return true;
}

} // namespace

PlanExecutor::PlanExecutor(Core *core) {
    if (core != nullptr && core->getStorageManager() != nullptr) {
        databaseManager = core->getStorageManager()->getDatabaseManager();
    } else {
        databaseManager = nullptr;
    }
}

ExecutionResult PlanExecutor::execute(std::shared_ptr<PlanNode> root,
                                        const std::string &dbName,
                                        const SelectStmt *selectStmt,
                                        ExecutionContext *executionContext) {
    if (root == nullptr) {
        return buildFailure("plan root is null", dbName);
    }

    if (databaseManager == nullptr) {
        return buildFailure("database manager is not initialized", dbName);
    }

    try {
        std::vector<std::vector<std::string>> resultSet;
        std::vector<std::string> columns;
        std::string resultMessage;
        std::string tableName;
        const auto whereCond = selectStmt != nullptr ? selectStmt->getWhereCondition() : nullptr;

        // 检测是否有 JOIN 节点
        // 作者：NAPH130
        bool hasJoin = false;
        bool hasAggregation = false;
        {
            std::function<void(const PlanNode *)> checkJoin;
            checkJoin = [&](const PlanNode *node) {
                if (node == nullptr) return;
                if (node->getNodeType() == PlanNodeType::NestedLoopJoin) {
                    hasJoin = true;
                }
                for (const auto &child : node->getChildren()) {
                    checkJoin(child.get());
                }
            };
            checkJoin(root.get());
        }

        if (hasJoin) {
            // 使用 JOIN 执行路径
            // 作者：NAPH130
            const bool whereHasSubquery = containsSubqueryCondition(whereCond.get());
            auto joinResult = executeJoinPlan(root,
                                              dbName,
                                              selectStmt,
                                              !whereHasSubquery,
                                              !whereHasSubquery);
            if (joinResult.columns.empty() && joinResult.rows.empty()) {
                return buildFailure("join query returned empty result schema", dbName);
            }

            resultSet.reserve(joinResult.rows.size());
            for (const auto &row : joinResult.rows) {
                resultSet.push_back(row.values);
            }
            columns = joinResult.columns;

            if (whereHasSubquery && whereCond != nullptr) {
                std::vector<std::vector<std::string>> filteredSet;
                filteredSet.reserve(resultSet.size());
                for (const auto &row : resultSet) {
                    if (evaluateConditionTreeWithDb(whereCond.get(), row, columns, dbName)) {
                        filteredSet.push_back(row);
                    }
                }
                resultSet = std::move(filteredSet);

                joinResult.rows.clear();
                joinResult.rows.reserve(resultSet.size());
                for (const auto &row : resultSet) {
                    joinResult.rows.push_back({row});
                }
                joinResult.columns = columns;
            }

            // 检测是否有聚合节点
            // 作者：NAPH130
            const AggregationPlanNode *aggNode = nullptr;
            {
                std::function<const AggregationPlanNode *(const PlanNode *)> findAgg;
                findAgg = [&](const PlanNode *node) -> const AggregationPlanNode * {
                    if (node == nullptr) return nullptr;
                    if (node->getNodeType() == PlanNodeType::Aggregation) {
                        return static_cast<const AggregationPlanNode *>(node);
                    }
                    for (const auto &child : node->getChildren()) {
                        const auto *found = findAgg(child.get());
                        if (found) return found;
                    }
                    return nullptr;
                };
                aggNode = findAgg(root.get());
                hasAggregation = (aggNode != nullptr);
            }

            if (aggNode != nullptr) {
                ExecutionResult aggResult = executeAggregation(joinResult.rows, joinResult.columns, aggNode);
                if (aggResult.getStatus() != ExecutionStatus::Success) {
                    return aggResult;
                }
                resultSet = aggResult.getResultSet();
                columns = aggResult.getColumns();
                resultMessage = "Join aggregation succeeded.";
            } else {
                if (whereHasSubquery && selectStmt != nullptr && !selectStmt->getSelectAllFields()) {
                    const ProjectionPlanNode *projNode = nullptr;
                    {
                        std::function<const ProjectionPlanNode *(const PlanNode *)> findProj;
                        findProj = [&](const PlanNode *node) -> const ProjectionPlanNode * {
                            if (node == nullptr) return nullptr;
                            if (node->getNodeType() == PlanNodeType::Projection) {
                                return static_cast<const ProjectionPlanNode *>(node);
                            }
                            for (const auto &child : node->getChildren()) {
                                const auto *found = findProj(child.get());
                                if (found) return found;
                            }
                            return nullptr;
                        };
                        projNode = findProj(root.get());
                    }

                    if (projNode != nullptr && !projNode->projectedColumns.empty()) {
                        std::vector<std::size_t> projIndexes;
                        std::vector<std::string> projCols;
                        for (const auto &colName : projNode->projectedColumns) {
                            if (colName.find('(') != std::string::npos) continue;
                            auto it = std::find(columns.begin(), columns.end(), colName);
                            if (it != columns.end()) {
                                projIndexes.push_back(
                                    static_cast<std::size_t>(std::distance(columns.begin(), it)));
                                projCols.push_back(colName);
                            }
                        }
                        if (!projIndexes.empty()) {
                            std::vector<std::vector<std::string>> projectedRows;
                            projectedRows.reserve(resultSet.size());
                            for (const auto &row : resultSet) {
                                std::vector<std::string> projectedRow;
                                projectedRow.reserve(projIndexes.size());
                                for (const auto idx : projIndexes) {
                                    projectedRow.push_back(idx < row.size() ? row[idx] : "");
                                }
                                projectedRows.push_back(std::move(projectedRow));
                            }
                            resultSet = std::move(projectedRows);
                            columns = std::move(projCols);
                        }
                    }
                }
                resultMessage = "Join query succeeded.";
            }
        } else {
            // 单表查询：提取 SeqScan 节点信息执行
            // 作者：NAPH130
            const SeqScanPlanNode *scanNode = nullptr;
            {
                std::function<const SeqScanPlanNode *(const PlanNode *)> findScan;
                findScan = [&](const PlanNode *node) -> const SeqScanPlanNode * {
                    if (node == nullptr) return nullptr;
                    if (node->getNodeType() == PlanNodeType::SeqScan) {
                        return static_cast<const SeqScanPlanNode *>(node);
                    }
                    for (const auto &child : node->getChildren()) {
                        const auto *found = findScan(child.get());
                        if (found) return found;
                    }
                    return nullptr;
                };
                scanNode = findScan(root.get());
            }

            if (scanNode == nullptr) {
                return buildFailure("no SeqScan node found in plan tree", dbName);
            }

            tableName = scanNode->tableName;

            auto table = storage::Table::load(SystemCatalogManager::getDataRootPath() / dbName, tableName);
            const auto &schema = table.schema();

            std::vector<storage::Table::WhereCondition> whereConditions;
            if (whereCond != nullptr) {
                // 将 ConditionNode 树转换为 WhereCondition 列表
                // 作者：NAPH130
                std::function<void(const ConditionNode *)> collectConditions;
                collectConditions = [&](const ConditionNode *node) {
                    if (node == nullptr) return;
                    const auto &leftNode = node->getLeftNode();
                    const auto &rightNode = node->getRightNode();
                    if (leftNode != nullptr || rightNode != nullptr) {
                        collectConditions(leftNode.get());
                        collectConditions(rightNode.get());
                        return;
                    }
                    const std::string &leftOp = node->getLeftOperand();
                    const std::string &op = node->getOperator();
                    const std::string &rightOp = node->getRightOperand();
                    if (!leftOp.empty() && !op.empty()) {
                        const std::string upperOp = toUpperString(op);
                        // BETWEEN/IN/NOT BETWEEN/NOT IN 由后置条件过滤处理
                        // 作者：NAPH130
                        if (upperOp == "BETWEEN" || upperOp == "NOT BETWEEN"
                            || upperOp == "IN" || upperOp == "NOT IN") {
                            return;
                        }
                        storage::Table::WhereCondition wc;
                        wc.column = leftOp;
                        wc.op = mapCompare(op);
                        wc.value = rightOp;
                        whereConditions.push_back(wc);
                    }
                };
                collectConditions(whereCond.get());
            }

            auto rows = table.select({}, whereConditions);
            resultSet.reserve(rows.size());
            for (const auto &row : rows) {
                resultSet.push_back(row.values);
            }
            columns = schema.columns;

            // 应用列投影（单表路径）
            // 作者：NAPH130
            const ProjectionPlanNode *projNode = nullptr;
            {
                std::function<const ProjectionPlanNode *(const PlanNode *)> findProj;
                findProj = [&](const PlanNode *node) -> const ProjectionPlanNode * {
                    if (node == nullptr) return nullptr;
                    if (node->getNodeType() == PlanNodeType::Projection) {
                        return static_cast<const ProjectionPlanNode *>(node);
                    }
                    for (const auto &child : node->getChildren()) {
                        const auto *found = findProj(child.get());
                        if (found) return found;
                    }
                    return nullptr;
                };
                projNode = findProj(root.get());
            }

            if (projNode != nullptr && !projNode->projectedColumns.empty() && !selectStmt->getSelectAllFields()) {
                std::vector<std::size_t> projIndexes;
                std::vector<std::string> projCols;
                for (const auto &colName : projNode->projectedColumns) {
                    // 跳过聚合函数名（已由聚合步骤处理）
                    if (colName.find('(') != std::string::npos) continue;
                    auto it = std::find(columns.begin(), columns.end(), colName);
                    if (it != columns.end()) {
                        projIndexes.push_back(static_cast<std::size_t>(std::distance(columns.begin(), it)));
                        projCols.push_back(colName);
                    }
                }
                if (!projIndexes.empty()) {
                    std::vector<std::vector<std::string>> projectedRows;
                    projectedRows.reserve(resultSet.size());
                    for (const auto &row : resultSet) {
                        std::vector<std::string> projectedRow;
                        projectedRow.reserve(projIndexes.size());
                        for (auto idx : projIndexes) {
                            projectedRow.push_back(idx < row.size() ? row[idx] : "");
                        }
                        projectedRows.push_back(std::move(projectedRow));
                    }
                    resultSet = std::move(projectedRows);
                    columns = std::move(projCols);
                }
            }

            // 检测聚合节点
            // 作者：NAPH130
            const AggregationPlanNode *aggNode = nullptr;
            {
                std::function<const AggregationPlanNode *(const PlanNode *)> findAgg;
                findAgg = [&](const PlanNode *node) -> const AggregationPlanNode * {
                    if (node == nullptr) return nullptr;
                    if (node->getNodeType() == PlanNodeType::Aggregation) {
                        return static_cast<const AggregationPlanNode *>(node);
                    }
                    for (const auto &child : node->getChildren()) {
                        const auto *found = findAgg(child.get());
                        if (found) return found;
                    }
                    return nullptr;
                };
                aggNode = findAgg(root.get());
                hasAggregation = (aggNode != nullptr);
            }

            if (aggNode != nullptr) {
                std::vector<storage::Row> rowObjs;
                rowObjs.reserve(resultSet.size());
                for (const auto &r : resultSet) {
                    rowObjs.push_back({r});
                }
                ExecutionResult aggResult = executeAggregation(rowObjs, columns, aggNode);
                if (aggResult.getStatus() != ExecutionStatus::Success) {
                    return aggResult;
                }
                resultSet = aggResult.getResultSet();
                columns = aggResult.getColumns();
                resultMessage = "Aggregation succeeded.";
            } else {
                resultMessage = "Select succeeded.";
            }
        }

        // 统一后处理：ORDER BY 和 LIMIT
        // 作者：NAPH130
        if (selectStmt != nullptr) {
            applyOrderByAndLimit(resultSet, columns, selectStmt);
        }

        // 单表路径下的后置 WHERE 条件过滤（仅在无聚合时执行，聚合的 WHERE 已在 table.select 中完成）
        // 作者：NAPH130
        if (!hasJoin && whereCond != nullptr && !hasAggregation) {
            std::vector<std::vector<std::string>> filteredSet;
            for (const auto &row : resultSet) {
                if (evaluateConditionTreeWithDb(whereCond.get(), row, columns, dbName)) {
                    filteredSet.push_back(row);
                }
            }
            resultSet = std::move(filteredSet);
        }

        return buildSuccess(resultMessage, resultSet, columns, dbName, tableName);
    } catch (const std::exception &e) {
        LogWriter::error("plan", "PlanExecutor", "execute", std::string("Plan execution failed: ") + e.what());
        return buildFailure(std::string("Plan execution failed: ") + e.what(), dbName);
    }
}

std::vector<storage::Row> PlanExecutor::executeSeqScan(const SeqScanPlanNode *node,
                                                         const std::string &tableName) {
    if (node == nullptr || databaseManager == nullptr) return {};

    return databaseManager->selectRows(node->dbName, tableName, {});
}

DatabaseManager::JoinResult PlanExecutor::executeJoinPlan(std::shared_ptr<PlanNode> root,
                                                            const std::string &dbName,
                                                            const SelectStmt *selectStmt,
                                                            const bool applyProjection,
                                                            const bool applyPostFilters) {
    DatabaseManager::JoinResult result;
    if (selectStmt == nullptr || databaseManager == nullptr) return result;

    // 从 Plan 树中收集 JOIN 节点信息和基表信息
    // 作者：NAPH130
    DatabaseManager::JoinQuery query;
    query.baseTable = selectStmt->getTableName();
    query.baseAlias = selectStmt->getTableName();

    // 收集 JOIN 节点
    // 作者：NAPH130
    std::function<void(const PlanNode *)> collectJoins;
    collectJoins = [&](const PlanNode *node) {
        if (node == nullptr) return;
        if (node->getNodeType() == PlanNodeType::NestedLoopJoin) {
            const auto *joinNode = static_cast<const NestedLoopJoinPlanNode *>(node);
            DatabaseManager::JoinSpec spec;
            if (joinNode->joinType == "INNER") {
                spec.type = DatabaseManager::JoinType::INNER_JOIN;
            } else if (joinNode->joinType == "LEFT") {
                spec.type = DatabaseManager::JoinType::LEFT_JOIN;
            } else if (joinNode->joinType == "RIGHT") {
                spec.type = DatabaseManager::JoinType::RIGHT_JOIN;
            }
            spec.tableName = joinNode->rightTableName;
            spec.alias = joinNode->rightAlias;

            // 将 ON 条件树转为 JoinCondition 列表
            // 作者：NAPH130
            if (joinNode->onCondition != nullptr) {
                // 简化：将叶子条件转为 JoinCondition
                // 作者：NAPH130
                const auto &leftOp = joinNode->onCondition->getLeftOperand();
                const auto &op = joinNode->onCondition->getOperator();
                const auto &rightOp = joinNode->onCondition->getRightOperand();
                if (!leftOp.empty() && !rightOp.empty()) {
                    DatabaseManager::JoinCondition cond;
                    // 解析 "alias.column" 格式
                    // 作者：NAPH130
                    const auto leftDot = leftOp.find('.');
                    const auto rightDot = rightOp.find('.');
                    cond.left.source = (leftDot != std::string::npos) ? leftOp.substr(0, leftDot) : "";
                    cond.left.column = (leftDot != std::string::npos) ? leftOp.substr(leftDot + 1) : leftOp;
                    cond.right.source = (rightDot != std::string::npos) ? rightOp.substr(0, rightDot) : "";
                    cond.right.column = (rightDot != std::string::npos) ? rightOp.substr(rightDot + 1) : rightOp;
                    cond.op = mapCompare(op);
                    spec.onConditions.push_back(std::move(cond));
                }
            }

            query.joins.push_back(std::move(spec));
        }
        for (const auto &child : node->getChildren()) {
            collectJoins(child.get());
        }
    };
    collectJoins(root.get());

    // 处理投影
    // 作者：NAPH130
    const ProjectionPlanNode *projNode = nullptr;
    {
        std::function<const ProjectionPlanNode *(const PlanNode *)> findProj;
        findProj = [&](const PlanNode *node) -> const ProjectionPlanNode * {
            if (node == nullptr) return nullptr;
            if (node->getNodeType() == PlanNodeType::Projection) {
                return static_cast<const ProjectionPlanNode *>(node);
            }
            for (const auto &child : node->getChildren()) {
                const auto *found = findProj(child.get());
                if (found) return found;
            }
            return nullptr;
        };
        projNode = findProj(root.get());
    }

    if (applyProjection && projNode != nullptr && !projNode->projectedColumns.empty()) {
        std::vector<DatabaseManager::JoinProjection> nonAggProjs;
        for (const auto &colName : projNode->projectedColumns) {
            // 跳过聚合函数列名（如 COUNT(*)、SUM(col)）
            // 这些会在后续的聚合步骤中处理
            // 作者：NAPH130
            if (colName.find('(') != std::string::npos) {
                continue;
            }
            DatabaseManager::JoinProjection proj;
            const auto dotPos = colName.find('.');
            if (dotPos != std::string::npos) {
                proj.column.source = colName.substr(0, dotPos);
                proj.column.column = colName.substr(dotPos + 1);
            } else {
                proj.column.source = "";
                proj.column.column = colName;
            }
            proj.outputName = colName;
            nonAggProjs.push_back(std::move(proj));
        }
        // 如果有非聚合投影列，使用它们；否则使用全列选择（让聚合步骤自行处理）
        // 作者：NAPH130
        if (!nonAggProjs.empty()) {
            query.projections = std::move(nonAggProjs);
        } else {
            // SELECT 全部列，后续聚合步骤会自行计算
            // 作者：NAPH130
            query.projections.clear();
        }
    }

    // 处理 WHERE 过滤
    // 作者：NAPH130
    const FilterPlanNode *filterNode = nullptr;
    {
        std::function<const FilterPlanNode *(const PlanNode *)> findFilter;
        findFilter = [&](const PlanNode *node) -> const FilterPlanNode * {
            if (node == nullptr) return nullptr;
            if (node->getNodeType() == PlanNodeType::Filter) {
                return static_cast<const FilterPlanNode *>(node);
            }
            for (const auto &child : node->getChildren()) {
                const auto *found = findFilter(child.get());
                if (found) return found;
            }
            return nullptr;
        };
        filterNode = findFilter(root.get());
    }

    if (applyPostFilters && filterNode != nullptr && filterNode->condition != nullptr) {
        // 递归收集所有叶子条件为 JoinFilter
        // 作者：NAPH130
        std::function<void(const ConditionNode *)> collectFilter;
        collectFilter = [&](const ConditionNode *node) {
            if (node == nullptr) return;
            const auto &l = node->getLeftNode();
            const auto &r = node->getRightNode();
            if (l != nullptr || r != nullptr) {
                collectFilter(l.get());
                collectFilter(r.get());
                return;
            }
            const std::string &leftOp = node->getLeftOperand();
            const std::string &op = node->getOperator();
            const std::string &rightOp = node->getRightOperand();
            if (!leftOp.empty() && !rightOp.empty()) {
                DatabaseManager::JoinFilter filter;
                filter.op = mapCompare(op);
                const auto hasArith = [](const std::string &s) {
                    return s.find(" + ") != std::string::npos
                        || s.find(" - ") != std::string::npos
                        || s.find(" * ") != std::string::npos
                        || s.find(" / ") != std::string::npos;
                };
                if (hasArith(leftOp)) {
                    filter.hasLeftExpr = true;
                    filter.leftExpr = leftOp;
                } else {
                    const auto leftDot = leftOp.find('.');
                    filter.column.source = (leftDot != std::string::npos) ? leftOp.substr(0, leftDot) : "";
                    filter.column.column = (leftDot != std::string::npos) ? leftOp.substr(leftDot + 1) : leftOp;
                }
                if (hasArith(rightOp)) {
                    filter.hasRightExpr = true;
                    filter.rightExpr = rightOp;
                } else {
                    const auto rightDot = rightOp.find('.');
                    if (rightDot != std::string::npos) {
                        filter.isColumnCompare = true;
                        filter.rightColumn.source = rightOp.substr(0, rightDot);
                        filter.rightColumn.column = rightOp.substr(rightDot + 1);
                    } else {
                        filter.value = rightOp;
                    }
                }
                query.postFilters.push_back(std::move(filter));
            }
        };
        collectFilter(filterNode->condition.get());
    }

    return databaseManager->selectJoinRows(dbName, query);
}

ExecutionResult PlanExecutor::executeAggregation(const std::vector<storage::Row> &rows,
                                                   const std::vector<std::string> &columns,
                                                   const AggregationPlanNode *aggregationNode) {
    if (aggregationNode == nullptr) {
        return buildFailure("aggregation node is null");
    }

    if (aggregationNode->aggregateExprs.empty()) {
        return buildFailure("no aggregate expressions");
    }

    // 建立列名到索引的映射
    // 作者：NAPH130
    std::map<std::string, std::size_t> columnIndex;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        columnIndex[columns[i]] = i;
    }

    // 分组
    // 作者：NAPH130
    const auto &groupByCols = aggregationNode->groupByColumns;
    std::map<std::string, std::vector<std::size_t>> groups;

    if (groupByCols.empty()) {
        // 无分组：所有行视为一组
        // 作者：NAPH130
        std::vector<std::size_t> allIndexes(rows.size());
        std::iota(allIndexes.begin(), allIndexes.end(), 0);
        groups[""] = std::move(allIndexes);
    } else {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            std::string groupKey;
            for (const auto &gbCol : groupByCols) {
                auto it = columnIndex.find(gbCol);
                if (it == columnIndex.end()) continue;
                if (!groupKey.empty()) groupKey += "\x1F";
                if (it->second < rows[i].values.size()) {
                    groupKey += rows[i].values[it->second];
                }
            }
            groups[groupKey].push_back(i);
        }
    }

    // 计算聚合
    // 作者：NAPH130
    std::vector<std::vector<std::string>> resultSet;
    std::vector<std::string> resultColumns;

    for (const auto &aggExpr : aggregationNode->aggregateExprs) {
        std::string colLabel;
        switch (aggExpr.op) {
            case storage::Table::AggregateOp::COUNT:
                colLabel = "COUNT(" + aggExpr.column + ")";
                break;
            case storage::Table::AggregateOp::SUM:
                colLabel = "SUM(" + aggExpr.column + ")";
                break;
            case storage::Table::AggregateOp::AVG:
                colLabel = "AVG(" + aggExpr.column + ")";
                break;
            case storage::Table::AggregateOp::MIN:
                colLabel = "MIN(" + aggExpr.column + ")";
                break;
            case storage::Table::AggregateOp::MAX:
                colLabel = "MAX(" + aggExpr.column + ")";
                break;
        }
        resultColumns.push_back(colLabel);
    }

    // 分组列名放在前面
    // 作者：NAPH130
    std::vector<std::string> outputCols = groupByCols;
    for (const auto &col : resultColumns) {
        outputCols.push_back(col);
    }
    resultColumns = outputCols;

    for (const auto &pair : groups) {
        const auto &rowIndexes = pair.second;
        std::vector<std::string> resultRow;

        // 分组键
        // 作者：NAPH130
        if (!groupByCols.empty()) {
            const auto &firstRow = rows[rowIndexes.front()];
            for (const auto &gbCol : groupByCols) {
                auto it = columnIndex.find(gbCol);
                if (it != columnIndex.end() && it->second < firstRow.values.size()) {
                    resultRow.push_back(firstRow.values[it->second]);
                } else {
                    resultRow.push_back("");
                }
            }
        }

        // 计算每个聚合函数
        // 作者：NAPH130
        for (const auto &aggExpr : aggregationNode->aggregateExprs) {
            switch (aggExpr.op) {
                case storage::Table::AggregateOp::COUNT: {
                    if (aggExpr.column == "*") {
                        resultRow.push_back(std::to_string(rowIndexes.size()));
                    } else {
                        auto it = columnIndex.find(aggExpr.column);
                        std::size_t cnt = 0;
                        if (it != columnIndex.end()) {
                            for (auto idx : rowIndexes) {
                                if (it->second < rows[idx].values.size()
                                    && !rows[idx].values[it->second].empty()) {
                                    ++cnt;
                                }
                            }
                        }
                        resultRow.push_back(std::to_string(cnt));
                    }
                    break;
                }
                case storage::Table::AggregateOp::SUM: {
                    auto it = columnIndex.find(aggExpr.column);
                    double sum = 0.0;
                    if (it != columnIndex.end()) {
                        for (auto idx : rowIndexes) {
                            if (it->second < rows[idx].values.size()) {
                                const double v = parseDoubleSafe(rows[idx].values[it->second]);
                                if (!std::isnan(v)) sum += v;
                            }
                        }
                    }
                    resultRow.push_back(std::to_string(sum));
                    break;
                }
                case storage::Table::AggregateOp::AVG: {
                    auto it = columnIndex.find(aggExpr.column);
                    double sum = 0.0;
                    std::size_t cnt = 0;
                    if (it != columnIndex.end()) {
                        for (auto idx : rowIndexes) {
                            if (it->second < rows[idx].values.size()) {
                                const double v = parseDoubleSafe(rows[idx].values[it->second]);
                                if (!std::isnan(v)) {
                                    sum += v;
                                    ++cnt;
                                }
                            }
                        }
                    }
                    const double avg = cnt > 0 ? sum / cnt : 0.0;
                    resultRow.push_back(std::to_string(avg));
                    break;
                }
                case storage::Table::AggregateOp::MIN: {
                    auto it = columnIndex.find(aggExpr.column);
                    std::string minVal;
                    bool first = true;
                    if (it != columnIndex.end()) {
                        for (auto idx : rowIndexes) {
                            if (it->second < rows[idx].values.size()) {
                                const auto &val = rows[idx].values[it->second];
                                if (first || val < minVal) {
                                    minVal = val;
                                    first = false;
                                }
                            }
                        }
                    }
                    resultRow.push_back(minVal);
                    break;
                }
                case storage::Table::AggregateOp::MAX: {
                    auto it = columnIndex.find(aggExpr.column);
                    std::string maxVal;
                    bool first = true;
                    if (it != columnIndex.end()) {
                        for (auto idx : rowIndexes) {
                            if (it->second < rows[idx].values.size()) {
                                const auto &val = rows[idx].values[it->second];
                                if (first || val > maxVal) {
                                    maxVal = val;
                                    first = false;
                                }
                            }
                        }
                    }
                    resultRow.push_back(maxVal);
                    break;
                }
            }
        }

        resultSet.push_back(std::move(resultRow));
    }

    // HAVING 过滤
    // 作者：NAPH130
    if (aggregationNode->havingCondition != nullptr) {
        std::vector<std::vector<std::string>> filteredSet;
        for (const auto &row : resultSet) {
            if (evaluateConditionTree(aggregationNode->havingCondition.get(), row, resultColumns)) {
                filteredSet.push_back(row);
            }
        }
        resultSet = std::move(filteredSet);
    }

    return buildSuccess("Aggregation succeeded.", resultSet, resultColumns);
}

bool PlanExecutor::evaluateConditionTree(const ConditionNode *node,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns) const {
    return evaluateConditionTreeWithDb(node, row, columns, "");
}

bool PlanExecutor::evaluateConditionTreeWithDb(const ConditionNode *node,
                                                const std::vector<std::string> &row,
                                                const std::vector<std::string> &columns,
                                                const std::string &dbName) const {
    if (node == nullptr) return true;

    // 子查询条件节点
    // 作者：NAPH130
    if (node->hasSubquery()) {
        return evaluateLeafCondition(node, row, columns, dbName);
    }

    const auto &leftNode = node->getLeftNode();
    const auto &rightNode = node->getRightNode();

    if (leftNode != nullptr || rightNode != nullptr) {
        const bool leftResult = evaluateConditionTreeWithDb(leftNode.get(), row, columns, dbName);
        const bool rightResult = evaluateConditionTreeWithDb(rightNode.get(), row, columns, dbName);
        std::string upper = node->getOperator();
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (upper == "AND") return leftResult && rightResult;
        return leftResult || rightResult;
    }

    return evaluateLeafCondition(node, row, columns, dbName);
}

bool PlanExecutor::evaluateLeafCondition(const ConditionNode *node,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns) const {
    return evaluateLeafCondition(node, row, columns, "");
}

bool PlanExecutor::evaluateLeafCondition(const ConditionNode *node,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns,
                                           const std::string &dbName) const {
    if (node == nullptr) return true;

    // 子查询条件
    // 作者：NAPH130
    if (node->hasSubquery()) {
        const auto subResult = evaluateSubquery(node->getSubquery().get(), dbName, row, columns);
        const std::string opUpper = toUpperString(node->getOperator());
        std::string leftValue;

        if (opUpper == "IN" || opUpper == "=") {
            if (!tryResolveValueFromRow(node->getLeftOperand(), row, columns, leftValue)) {
                return false;
            }
            bool found = std::find(subResult.begin(), subResult.end(), leftValue) != subResult.end();
            return node->isNegated() ? !found : found;
        }
        if (opUpper == "EXISTS") {
            return node->isNegated() ? subResult.empty() : !subResult.empty();
        }
        return false;
    }

    const std::string &columnName = node->getLeftOperand();
    const std::string &opStr = node->getOperator();
    const std::string &value = node->getRightOperand();

    std::string leftValue;
    if (!tryResolveValueFromRow(columnName, row, columns, leftValue)) return false;

    const std::string upperOp = toUpperString(opStr);

    // BETWEEN 处理
    // 作者：NAPH130
    if (upperOp == "BETWEEN" || upperOp == "NOT BETWEEN") {
        const std::string &range = value;
        const auto andPos = range.find(" AND ");
        if (andPos == std::string::npos) return false;
        const std::string lowStr = range.substr(0, andPos);
        const std::string highStr = range.substr(andPos + 5);
        const bool inRange = compareValues(leftValue, mapCompare(">="), lowStr)
                             && compareValues(leftValue, mapCompare("<="), highStr);
        return upperOp == "BETWEEN" ? inRange : !inRange;
    }

    // IN 处理
    // 作者：NAPH130
    if (upperOp == "IN" || upperOp == "NOT IN") {
        const std::string &valList = value;
        std::string inner = valList;
        // 去掉外层括号
        if (!inner.empty() && inner.front() == '(') inner = inner.substr(1);
        if (!inner.empty() && inner.back() == ')') inner.pop_back();
        const auto items = storage::split(inner, ',');
        bool found = false;
        for (const auto &item : items) {
            // 手动去除前后空格
            std::string trimmed = item;
            trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
            trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
            if (compareValues(leftValue, mapCompare("="), trimmed)) {
                found = true;
                break;
            }
        }
        return upperOp == "IN" ? found : !found;
    }

    std::string resolvedRight = value;
    std::string rightValueFromRow;
    if (tryResolveValueFromRow(value, row, columns, rightValueFromRow)) {
        resolvedRight = rightValueFromRow;
    }
    return compareValues(leftValue, mapCompare(opStr), resolvedRight);
}

std::vector<std::string> PlanExecutor::evaluateSubquery(const SQLStatement *subquery,
                                                          const std::string &dbName,
                                                          const std::vector<std::string> &outerRow,
                                                          const std::vector<std::string> &outerColumns) const {
    if (subquery == nullptr || databaseManager == nullptr) return {};

    if (subquery->getStmtType() != ExecutionStatementType::Select) return {};

    const auto *selStmt = static_cast<const SelectStmt *>(subquery);
    const std::string tableName = selStmt->getTableName();

    try {
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        auto table = storage::Table::load(dbPath, tableName);
        const auto &schema = table.schema();

        /**
         * @brief 将 "table.column" 形式剥离为纯列名
         * @author NAPH130
         */
        auto stripTablePrefix = [&tableName](const std::string &colRef) -> std::string {
            const auto dotPos = colRef.find('.');
            if (dotPos == std::string::npos) return colRef;
            return colRef.substr(dotPos + 1);
        };

        /**
         * @brief 解析外表引用：若引用含 "table.col" 且 table 不同于子查询表名，则从外部行取值
         * @author NAPH130
         */
        auto resolveOuterRef = [&](const std::string &val) -> std::string {
            const auto dotPos = val.find('.');
            if (dotPos == std::string::npos) return val;
            const std::string refTable = val.substr(0, dotPos);
            // 引用同一张表 → 不替换
            if (refTable == tableName) return val;
            // 引用外表 → 从当前外部行查找列值
            std::string resolved;
            if (tryResolveValueFromRow(val, outerRow, outerColumns, resolved)) {
                return resolved;
            }
            const std::string refCol = val.substr(dotPos + 1);
            if (tryResolveValueFromRow(refCol, outerRow, outerColumns, resolved)) {
                return resolved;
            }
            return val;
        };

        std::vector<storage::Table::WhereCondition> whereConditions;
        const auto whereCond = selStmt->getWhereCondition();
        if (whereCond != nullptr) {
            std::function<void(const ConditionNode *)> collect;
            collect = [&](const ConditionNode *node) {
                if (node == nullptr) return;
                const auto &l = node->getLeftNode();
                const auto &r = node->getRightNode();
                if (l != nullptr || r != nullptr) {
                    collect(l.get()); collect(r.get());
                    return;
                }
                const std::string &leftOp = node->getLeftOperand();
                const std::string &op = node->getOperator();
                const std::string &rightOp = node->getRightOperand();
                if (!leftOp.empty() && !op.empty()) {
                    const std::string upper = toUpperString(op);
                    if (upper == "BETWEEN" || upper == "NOT BETWEEN"
                        || upper == "IN" || upper == "NOT IN") {
                        return;
                    }
                    storage::Table::WhereCondition wc;
                    wc.column = stripTablePrefix(leftOp);
                    wc.op = mapCompare(op);
                    wc.value = resolveOuterRef(rightOp);
                    whereConditions.push_back(wc);
                }
            };
            collect(whereCond.get());
        }

        auto rows = table.select({}, whereConditions);
        std::size_t projectedIndex = 0;
        if (!selStmt->getSelectAllFields() && !selStmt->getTargetFields().empty()) {
            const std::string projectedName = stripTablePrefix(selStmt->getTargetFields().front());
            auto projectedIt = std::find(schema.columns.begin(), schema.columns.end(), projectedName);
            if (projectedIt != schema.columns.end()) {
                projectedIndex =
                    static_cast<std::size_t>(std::distance(schema.columns.begin(), projectedIt));
            }
        }
        std::vector<std::string> result;
        result.reserve(rows.size());
        for (const auto &row : rows) {
            if (projectedIndex < row.values.size()) {
                result.push_back(row.values[projectedIndex]);
            }
        }
        return result;
    } catch (...) {
        return {};
    }
}

bool PlanExecutor::compareValues(const std::string &left,
                                   storage::Table::CompareOp op,
                                   const std::string &right) {
    if (op == storage::Table::CompareOp::LIKE) {
        return likeMatch(left, right);
    }

    // NULL（空字符串）与任何值比较均返回 false
    // 作者：NAPH130
    if (left.empty() || right.empty()) {
        return false;
    }

    double lnum = 0.0, rnum = 0.0;
    bool lIsNum = false, rIsNum = false;

    try {
        std::size_t pos = 0;
        lnum = std::stod(left, &pos);
        lIsNum = (pos == left.size());
    } catch (...) { lIsNum = false; }

    try {
        std::size_t pos = 0;
        rnum = std::stod(right, &pos);
        rIsNum = (pos == right.size());
    } catch (...) { rIsNum = false; }

    if (lIsNum && rIsNum) {
        switch (op) {
            case storage::Table::CompareOp::EQ: return lnum == rnum;
            case storage::Table::CompareOp::NE: return lnum != rnum;
            case storage::Table::CompareOp::GT: return lnum > rnum;
            case storage::Table::CompareOp::GE: return lnum >= rnum;
            case storage::Table::CompareOp::LT: return lnum < rnum;
            case storage::Table::CompareOp::LE: return lnum <= rnum;
            default: return false;
        }
    }

    switch (op) {
        case storage::Table::CompareOp::EQ: return left == right;
        case storage::Table::CompareOp::NE: return left != right;
        case storage::Table::CompareOp::GT: return left > right;
        case storage::Table::CompareOp::GE: return left >= right;
        case storage::Table::CompareOp::LT: return left < right;
        case storage::Table::CompareOp::LE: return left <= right;
        default: return false;
    }
}

bool PlanExecutor::likeMatch(const std::string &text, const std::string &pattern) {
    std::size_t t = 0, p = 0, star = std::string::npos, match = 0;
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == text[t] || pattern[p] == '_')) { ++t; ++p; continue; }
        if (p < pattern.size() && pattern[p] == '%') { star = p++; match = t; continue; }
        if (star != std::string::npos) { p = star + 1; t = ++match; continue; }
        return false;
    }
    while (p < pattern.size() && pattern[p] == '%') ++p;
    return p == pattern.size();
}

void PlanExecutor::applyOrderByAndLimit(std::vector<std::vector<std::string>> &resultSet,
                                          std::vector<std::string> &columns,
                                          const SelectStmt *selectStmt) {
    if (selectStmt == nullptr || resultSet.empty()) {
        return;
    }

    // ORDER BY 排序
    // 作者：NAPH130
    const std::string &orderByColumn = selectStmt->getOrderByColumn();
    if (!orderByColumn.empty() && !columns.empty()) {
        // 剥离 " DESC" / " ASC" 后缀
        // 作者：NAPH130
        std::string cleanCol = orderByColumn;
        const auto descPos = cleanCol.find(" DESC");
        const auto ascPos = cleanCol.find(" ASC");
        if (descPos != std::string::npos) cleanCol = cleanCol.substr(0, descPos);
        else if (ascPos != std::string::npos) cleanCol = cleanCol.substr(0, ascPos);

        auto it = std::find(columns.begin(), columns.end(), cleanCol);
        if (it != columns.end()) {
            const std::size_t orderIdx = static_cast<std::size_t>(std::distance(columns.begin(), it));
            const bool desc = selectStmt->getOrderByDesc();
            std::stable_sort(resultSet.begin(), resultSet.end(),
                             [orderIdx, desc](const std::vector<std::string> &a,
                                              const std::vector<std::string> &b) {
                                 if (orderIdx >= a.size() || orderIdx >= b.size()) return false;
                                 const std::string &av = a[orderIdx];
                                 const std::string &bv = b[orderIdx];
                                 double an = 0.0, bn = 0.0;
                                 const bool aNum = storage::tryParseNumber(av, an);
                                 const bool bNum = storage::tryParseNumber(bv, bn);
                                 if (aNum && bNum) {
                                     return desc ? (an > bn) : (an < bn);
                                 }
                                 return desc ? (av > bv) : (av < bv);
                             });
        }
    }

    // LIMIT 截断
    // 作者：NAPH130
    if (selectStmt->getHasLimit()) {
        const std::size_t limit = selectStmt->getLimitCount();
        if (limit < resultSet.size()) {
            resultSet.resize(limit);
        }
    }
}
