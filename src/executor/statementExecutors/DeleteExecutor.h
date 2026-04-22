#pragma once

#include "models/parser/DeleteStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "../StatementExecutor.h"

/**
 * @class DeleteExecutor
 * @brief DELETE 语句执行器
 * @details 负责处理 DELETE FROM 语句最小执行链路接入，当前返回 stub 执行结果。
 * @author YuzhSong
 */
class DeleteExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     * @param core 服务端核心对象指针
     * @param databaseManager 数据库管理器指针
     */
    DeleteExecutor(Core *core, DatabaseManager *databaseManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author YuzhSong
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行 DELETE 语句
     * @author YuzhSong
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    DatabaseManager *databaseManager;
};
