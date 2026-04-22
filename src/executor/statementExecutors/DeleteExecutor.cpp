#include "DeleteExecutor.h"

namespace
{
/**
 * @brief 构建失败执行结果
 * @author YuzhSong
 * @param message 英文失败消息
 * @return 失败执行结果
 */
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}
} // namespace

DeleteExecutor::DeleteExecutor(Core *core, DatabaseManager *databaseManager)
    : StatementExecutor(core), databaseManager(databaseManager)
{
}

ExecutionStatementType DeleteExecutor::getSupportedType() const
{
    return ExecutionStatementType::Delete;
}

ExecutionResult DeleteExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    (void)executionContext;
    (void)databaseManager;
    if (statement == nullptr) {
        return buildFailureResult("DeleteExecutor received null statement pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("DeleteExecutor received mismatched statement type.");
    }

    const auto *deleteStmt = static_cast<const DeleteStmt *>(statement);
    if (deleteStmt->getTableName().empty()) {
        return buildFailureResult("DELETE statement requires a non-empty table name.");
    }

    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage("DELETE statement accepted in stub mode.");
    executionResult.setAffectedRows(0);
    return executionResult;
}
