#include "UseDbExecutor.h"

#include "log/LogWriter.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message, const std::string &dbName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}

ExecutionResult buildSuccessResult(const std::string &message, const std::string &dbName)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}
}

UseDbExecutor::UseDbExecutor(Core *core, SystemCatalogManager *systemCatalogManager)
    : StatementExecutor(core), systemCatalogManager(systemCatalogManager)
{
}

ExecutionStatementType UseDbExecutor::getSupportedType() const
{
    return ExecutionStatementType::UseDatabase;
}

ExecutionResult UseDbExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "UseDbExecutor", "execute", "Use database input pointer is invalid.");
        return buildFailureResult("UseDbExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "UseDbExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("UseDbExecutor received mismatched statement type.");
    }

    return executeUseDb(static_cast<const UseDbStmt *>(statement), executionContext);
}

ExecutionResult UseDbExecutor::executeUseDb(const UseDbStmt *useDbStmt, ExecutionContext *executionContext)
{
    if (useDbStmt == nullptr || executionContext == nullptr) {
        return buildFailureResult("Use database input is invalid.");
    }

    if (systemCatalogManager == nullptr) {
        return buildFailureResult("System catalog manager is not initialized.");
    }

    const std::string &dbName = useDbStmt->getDbName();
    if (dbName.empty()) {
        LogWriter::warning("executor", "UseDbExecutor", "executeUseDb", "Database name is empty.");
        return buildFailureResult("Database name cannot be empty.");
    }

    if (!systemCatalogManager->checkDbExists(dbName)) {
        LogWriter::warning("executor", "UseDbExecutor", "executeUseDb", "Database does not exist: " + dbName + ".");
        return buildFailureResult("Database does not exist.", dbName);
    }

    executionContext->setCurrentDbName(dbName);
    LogWriter::info("executor", "UseDbExecutor", "executeUseDb", "Database switched successfully: " + dbName + ".");
    return buildSuccessResult("Use database succeeded.", dbName);
}
