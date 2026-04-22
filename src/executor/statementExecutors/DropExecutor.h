#pragma once

#include "models/parser/DropStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "../StatementExecutor.h"

/**
 * @class DropExecutor
 * @brief DROP 语句执行器
 * @details 负责处理 DROP DATABASE 与 DROP TABLE 的最小执行链路接入，当前以 stub 模式返回。
 * @author YuzhSong
 */
class DropExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     * @param core 服务端核心对象指针
     * @param systemCatalogManager 系统目录管理器指针
     * @param databaseManager 数据库管理器指针
     */
    DropExecutor(Core *core, SystemCatalogManager *systemCatalogManager, DatabaseManager *databaseManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author YuzhSong
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行 DROP 语句
     * @author YuzhSong
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    SystemCatalogManager *systemCatalogManager;
    DatabaseManager *databaseManager;
};
