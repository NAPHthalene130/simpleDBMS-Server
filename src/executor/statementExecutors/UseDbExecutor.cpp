#include "UseDbExecutor.h"

#include "log/LogWriter.h"

namespace {
/**
 * @brief 构建失败执行结果
 * @author NAPH130
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
}

UseDbExecutor::UseDbExecutor(Core *core, SystemCatalogManager *systemCatalogManager)
    : StatementExecutor(core), systemCatalogManager(systemCatalogManager)
{
    LogWriter::debug("executor", "UseDbExecutor", "UseDbExecutor", "Use database executor initialized.");
}

ExecutionStatementType UseDbExecutor::getSupportedType() const
{
    return ExecutionStatementType::UseDatabase;
}

ExecutionResult UseDbExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "UseDbExecutor", "execute", "Use database executor received null input pointer.");
        return buildFailureResult("UseDbExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "UseDbExecutor", "execute", "Use database executor received mismatched statement type.");
        return buildFailureResult("UseDbExecutor received mismatched statement type.");
    }

    return executeUseDb(static_cast<const UseDbStmt *>(statement), executionContext);
}

ExecutionResult UseDbExecutor::executeUseDb(const UseDbStmt *useDbStmt, ExecutionContext *executionContext)
{
    if (useDbStmt == nullptr || systemCatalogManager == nullptr) {
        LogWriter::error("executor", "UseDbExecutor", "executeUseDb", "Use database dependencies are not initialized.");
        return buildFailureResult("Use database dependencies are not initialized.");
    }

    const std::string &dbName = useDbStmt->getDbName();
    if (dbName.empty()) {
        LogWriter::warning("executor", "UseDbExecutor", "executeUseDb", "Received empty database name.");
        return buildFailureResult("USE statement requires a non-empty database name.");
    }

    if (!systemCatalogManager->checkDbExists(dbName)) {
        LogWriter::warning("executor", "UseDbExecutor", "executeUseDb", "Target database does not exist: " + dbName + ".");
        return buildFailureResult("Database does not exist: " + dbName + ".");
    }

    executionContext->setCurrentDbName(dbName);
    LogWriter::info("executor", "UseDbExecutor", "executeUseDb", "Current database switched to " + dbName + ".");

    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage("Database switched successfully: " + dbName + ".");
    executionResult.setAffectedRows(0);
    executionResult.setDbName(dbName);
    return executionResult;
}
