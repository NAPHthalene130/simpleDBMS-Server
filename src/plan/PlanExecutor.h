#pragma once

#include <memory>
#include <string>
#include <vector>

#include "models/executor/ExecutionContext.h"
#include "models/executor/ExecutionResult.h"
#include "models/parser/ConditionNode.h"
#include "models/parser/SelectStmt.h"
#include "plan/PlanNode.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/object/StorageCommon.h"

class Core;

/**
 * @class PlanExecutor
 * @brief Plan 树执行器
 * @details 递归遍历 PlanNode 树并执行各节点语义，将中间结果自底向上传递最终产出 ExecutionResult。
 * @author NAPH130
 */
class PlanExecutor {
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit PlanExecutor(Core *core);

    /**
     * @brief 执行计划树
     * @author NAPH130
     * @param root 计划树根节点
     * @param dbName 当前数据库名
     * @param selectStmt 原始 SELECT AST（用于回退列名等）
     * @param executionContext 执行上下文
     * @return 执行结果
     */
    ExecutionResult execute(std::shared_ptr<PlanNode> root,
                            const std::string &dbName,
                            const SelectStmt *selectStmt,
                            ExecutionContext *executionContext);

private:
    /**
     * @brief 执行全表扫描节点
     * @author NAPH130
     * @param node SeqScan 节点
     * @param tableName 表名
     * @return 行数据列表
     */
    std::vector<storage::Row> executeSeqScan(const SeqScanPlanNode *node, const std::string &tableName);

    /**
     * @brief 将 PlanNode 树转换为 DatabaseManager::JoinQuery 并执行
     * @author NAPH130
     * @param root Plan 树根节点
     * @param dbName 数据库名
     * @param selectStmt 原始 SELECT
     * @return JOIN 结果
     */
    DatabaseManager::JoinResult executeJoinPlan(std::shared_ptr<PlanNode> root,
                                                  const std::string &dbName,
                                                  const SelectStmt *selectStmt);

    /**
     * @brief 执行聚合计算
     * @author NAPH130
     * @param rows 输入行数据
     * @param columns 列名列表
     * @param aggregationNode 聚合节点
     * @return 执行结果
     */
    ExecutionResult executeAggregation(const std::vector<storage::Row> &rows,
                                         const std::vector<std::string> &columns,
                                         const AggregationPlanNode *aggregationNode);

    /**
     * @brief 评估条件树
     * @author NAPH130
     */
    bool evaluateConditionTree(const ConditionNode *node,
                               const std::vector<std::string> &row,
                               const std::vector<std::string> &columns) const;

    bool evaluateLeafCondition(const ConditionNode *node,
                               const std::vector<std::string> &row,
                               const std::vector<std::string> &columns) const;

    static bool compareValues(const std::string &left, storage::Table::CompareOp op, const std::string &right);
    static bool likeMatch(const std::string &text, const std::string &pattern);

    /**
     * @brief 对结果集应用 ORDER BY 排序和 LIMIT 截断
     * @author NAPH130
     * @param resultSet 结果集（会被原地修改）
     * @param columns 当前输出的列名列表
     * @param selectStmt 原始 SELECT 语句
     * @return 最终列名列表（聚合或投影后可能与 columns 不同）
     */
    static void applyOrderByAndLimit(std::vector<std::vector<std::string>> &resultSet,
                                      std::vector<std::string> &columns,
                                      const SelectStmt *selectStmt);

    Core *core;
    DatabaseManager *databaseManager;
};
