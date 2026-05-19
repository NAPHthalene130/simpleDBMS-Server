#pragma once

#include "models/parser/DclStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "../StatementExecutor.h"

/**
 * @class DclExecutor
 * @brief DCL 语句执行器（GRANT / REVOKE）
 * @details 负责处理用户权限管理操作。当前阶段 GRANT 用于创建用户并设置密码，
 *          REVOKE 用于删除用户。
 * @author NAPH130
 */
class DclExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param databaseManager 数据库管理器指针
     */
    DclExecutor(Core *core, DatabaseManager *databaseManager);

    ExecutionStatementType getSupportedType() const override;

    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    DatabaseManager *databaseManager;
};
