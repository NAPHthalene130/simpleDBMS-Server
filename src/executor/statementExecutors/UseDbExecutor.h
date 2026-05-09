#pragma once

#include "models/parser/UseDbStmt.h"
#include "storage/manager/SystemCatalogManager.h"
#include "../StatementExecutor.h"

/**
 * @class UseDbExecutor
 * @brief 切换数据库语句执行器
 * @details 负责校验目标数据库是否存在，并将执行上下文切换到目标数据库。
 * @author NAPH130
 */
class UseDbExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param systemCatalogManager 系统目录管理器指针
     */
    UseDbExecutor(Core *core, SystemCatalogManager *systemCatalogManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行切换数据库语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    /**
     * @brief 执行切换数据库核心流程
     * @author NAPH130
     * @param useDbStmt 切换数据库语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeUseDb(const UseDbStmt *useDbStmt, ExecutionContext *executionContext);

private:
    SystemCatalogManager *systemCatalogManager;
};
