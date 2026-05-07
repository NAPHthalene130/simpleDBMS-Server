#pragma once

#include "models/parser/ShowStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "../StatementExecutor.h"

/**
 * @class ShowExecutor
 * @brief SHOW 语句执行器
 * @details 负责处理 SHOW DATABASES / TABLES / DATABASE / TABLE 并返回实际结果集。
 * @author NAPH130
 */
class ShowExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param systemCatalogManager 系统目录管理器指针
     * @param databaseManager 数据库管理器指针
     */
    ShowExecutor(Core *core,
                 SystemCatalogManager *systemCatalogManager,
                 DatabaseManager *databaseManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author YuzhSong
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行 SHOW 语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    SystemCatalogManager *systemCatalogManager;
    DatabaseManager *databaseManager;
};
