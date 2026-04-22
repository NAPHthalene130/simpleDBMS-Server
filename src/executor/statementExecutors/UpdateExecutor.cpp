#include "UpdateExecutor.h"

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

UpdateExecutor::UpdateExecutor(Core *core, DatabaseManager *databaseManager)
    : StatementExecutor(core), databaseManager(databaseManager)
{
}

ExecutionStatementType UpdateExecutor::getSupportedType() const
{
    return ExecutionStatementType::Update;
}

ExecutionResult UpdateExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    (void)executionContext;
    (void)databaseManager;
    if (statement == nullptr) {
        return buildFailureResult("UpdateExecutor received null statement pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("UpdateExecutor received mismatched statement type.");
    }

    const auto *updateStmt = static_cast<const UpdateStmt *>(statement);
    if (updateStmt->getTableName().empty()) {
        return buildFailureResult("UPDATE statement requires a non-empty table name.");
    }
    if (updateStmt->getColumnNames().empty() || updateStmt->getColumnNames().size() != updateStmt->getValues().size()) {
        return buildFailureResult("UPDATE statement assignments are invalid.");
    }

    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage("UPDATE statement accepted in stub mode.");
    executionResult.setAffectedRows(0);
    return executionResult;
}
