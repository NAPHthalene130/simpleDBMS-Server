#pragma once

#include <vector>

#include "models/parser/ConditionNode.h"
#include "models/parser/UpdateStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/object/Table.h"
#include "../StatementExecutor.h"

/**
 * @class UpdateExecutor
 * @brief UPDATE 语句执行器
 * @details 负责处理 UPDATE 语句的 SET 赋值、WHERE 条件过滤与数据更新流程。
 * @author NAPH130
 */
class UpdateExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param databaseManager 数据库管理器指针
     */
    UpdateExecutor(Core *core, DatabaseManager *databaseManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行 UPDATE 语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    /**
     * @brief 执行 UPDATE 核心流程
     * @author NAPH130
     * @param updateStmt UPDATE 语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeUpdate(const UpdateStmt *updateStmt, ExecutionContext *executionContext);

    /**
     * @brief 递归评估条件树节点对一行数据是否成立
     * @author NAPH130
     * @param conditionNode 条件树节点
     * @param row 行数据值列表
     * @param columns 表列名列表
     * @return 是否满足条件
     */
    static bool evaluateConditionTree(const ConditionNode *conditionNode,
                                      const std::vector<std::string> &row,
                                      const std::vector<std::string> &columns,
                                      const std::string &dbName,
                                      const std::string &tableName);

    /**
     * @brief 评估叶子比较节点对一行数据是否成立
     * @author NAPH130
     * @param conditionNode 叶子条件节点
     * @param row 行数据值列表
     * @param columns 表列名列表
     * @return 是否满足条件
     */
    static bool evaluateLeafCondition(const ConditionNode *conditionNode,
                                      const std::vector<std::string> &row,
                                      const std::vector<std::string> &columns,
                                      const std::string &dbName,
                                      const std::string &tableName);

    /**
     * @brief 比较两个值是否满足指定比较操作
     * @author NAPH130
     * @param leftValue 左值
     * @param op 比较操作符
     * @param rightValue 右值
     * @return 比较结果
     */
    static bool compareValues(const std::string &leftValue,
                              storage::Table::CompareOp op,
                              const std::string &rightValue);

    /**
     * @brief LIKE 模式匹配
     * @author NAPH130
     * @param text 待匹配文本
     * @param pattern LIKE 模式
     * @return 是否匹配
     */
    static bool likeMatch(const std::string &text, const std::string &pattern);

private:
    DatabaseManager *databaseManager;
};
