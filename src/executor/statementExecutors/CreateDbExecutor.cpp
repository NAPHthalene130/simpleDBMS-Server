#include "CreateDbExecutor.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}
}

CreateDbExecutor::CreateDbExecutor(SystemCatalogManager &systemCatalogManager)
    : systemCatalogManager(systemCatalogManager)
{
}

ExecutionStatementType CreateDbExecutor::getSupportedType() const
{
    return ExecutionStatementType::CreateDatabase;
}

ExecutionResult CreateDbExecutor::execute(const SQLStatement &statement, ExecutionContext &executionContext)
{
    if (statement.getStmtType() != getSupportedType()) {
        return buildFailureResult("CreateDbExecutor received mismatched statement type.");
    }

    return executeCreateDb(static_cast<const CreateDbStmt &>(statement), executionContext);
}

ExecutionResult CreateDbExecutor::executeCreateDb(const CreateDbStmt &createDbStmt, ExecutionContext &executionContext)
{
    (void)createDbStmt;
    (void)executionContext;
    return buildFailureResult("CreateDbExecutor is registered, but execution logic is not implemented yet.");
}

DatabaseBlock CreateDbExecutor::buildDatabaseBlock(const CreateDbStmt &createDbStmt) const
{
    (void)createDbStmt;
    return DatabaseBlock();
}
