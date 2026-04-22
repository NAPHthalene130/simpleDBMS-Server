#include "DropExecutor.h"

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

DropExecutor::DropExecutor(Core *core, SystemCatalogManager *systemCatalogManager, DatabaseManager *databaseManager)
    : StatementExecutor(core), systemCatalogManager(systemCatalogManager), databaseManager(databaseManager)
{
}

ExecutionStatementType DropExecutor::getSupportedType() const
{
    return ExecutionStatementType::Drop;
}

ExecutionResult DropExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    (void)executionContext;
    (void)systemCatalogManager;
    (void)databaseManager;
    if (statement == nullptr) {
        return buildFailureResult("DropExecutor received null statement pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("DropExecutor received mismatched statement type.");
    }

    const auto *dropStmt = static_cast<const DropStmt *>(statement);
    if (dropStmt->getTargetName().empty()) {
        return buildFailureResult("DROP statement requires a non-empty target name.");
    }

    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage("DROP statement accepted in stub mode.");
    executionResult.setAffectedRows(0);
    return executionResult;
}
