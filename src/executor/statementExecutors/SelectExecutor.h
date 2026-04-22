#pragma once

#include <vector>

#include "models/parser/ConditionNode.h"
#include "models/parser/SelectStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/manager/TableDefManager.h"
#include "../StatementExecutor.h"

/**
 * @class SelectExecutor
 * @brief 查询语句执行器
 * @details 负责处理 SELECT 语句的字段投影、条件过滤与结果集封装流程。
 * @author NAPH130
 */
class SelectExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param systemCatalogManager 系统目录管理器指针
     * @param databaseManager 数据库管理器指针
     * @param tableDefManager 表定义管理器指针
     */
    SelectExecutor(Core *core,
                   SystemCatalogManager *systemCatalogManager,
                   DatabaseManager *databaseManager,
                   TableDefManager *tableDefManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行查询语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    /**
     * @brief 执行查询语句核心流程
     * @author NAPH130
     * @param selectStmt 查询语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeSelect(const SelectStmt *selectStmt, ExecutionContext *executionContext);

    /**
     * @brief 校验目标字段是否满足查询要求
     * @author NAPH130
     * @param selectStmt 查询语句对象
     * @return 是否通过字段校验
     */
    bool validateTargetFields(const SelectStmt *selectStmt) const;

    /**
     * @brief 评估条件树是否满足过滤要求
     * @author NAPH130
     * @param conditionNode 条件树节点
     * @return 是否满足条件
     */
    bool evaluateCondition(const ConditionNode *conditionNode) const;

    /**
     * @brief 构建查询结果集
     * @author NAPH130
     * @param selectStmt 查询语句对象
     * @return 结果集二维数组
     */
    std::vector<std::vector<std::string>> buildResultSet(const SelectStmt *selectStmt) const;

    /**
     * @brief 判断是否为数据库列表查询
     * @author NAPH130
     * @param selectStmt 查询语句对象
     * @return 是否为数据库元数据查询
     */
    bool isShowDatabaseQuery(const SelectStmt *selectStmt) const;

    /**
     * @brief 判断是否为数据表列表查询
     * @author NAPH130
     * @param selectStmt 查询语句对象
     * @return 是否为数据表元数据查询
     */
    bool isShowTablesQuery(const SelectStmt *selectStmt) const;

    /**
     * @brief 校验元数据查询字段是否合法
     * @author NAPH130
     * @param selectStmt 查询语句对象
     * @return 是否通过字段校验
     */
    bool validateMetadataFields(const SelectStmt *selectStmt) const;

private:
    SystemCatalogManager *systemCatalogManager;
    DatabaseManager *databaseManager;
    TableDefManager *tableDefManager;
};
