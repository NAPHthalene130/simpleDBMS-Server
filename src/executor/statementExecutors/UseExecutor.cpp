#include "UseExecutor.h"

#include "log/LogWriter.h"

namespace
{
/**
 * @brief 构建失败执行结果
 * @author NAPH130
 * @param message 英文失败消息
 * @param dbName 可选的数据库名称
 * @return 失败执行结果
 */
ExecutionResult buildFailureResult(const std::string &message, const std::string &dbName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}

/**
 * @brief 构建成功执行结果
 * @author NAPH130
 * @param message 英文成功消息
 * @param dbName 数据库名称
 * @return 成功执行结果
 */
ExecutionResult buildSuccessResult(const std::string &message, const std::string &dbName)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}
} // namespace

UseExecutor::UseExecutor(Core *core, SystemCatalogManager *systemCatalogManager)
    : StatementExecutor(core), systemCatalogManager(systemCatalogManager)
{
}

ExecutionStatementType UseExecutor::getSupportedType() const
{
    return ExecutionStatementType::Use;
}

ExecutionResult UseExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "UseExecutor", "execute", "Use input pointer is invalid.");
        return buildFailureResult("UseExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "UseExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("UseExecutor received mismatched statement type.");
    }

    return executeUse(static_cast<const UseStmt *>(statement), executionContext);
}

ExecutionResult UseExecutor::executeUse(const UseStmt *useStmt, ExecutionContext *executionContext)
{
    if (useStmt == nullptr || executionContext == nullptr) {
        return buildFailureResult("Use input is invalid.");
    }

    if (systemCatalogManager == nullptr) {
        return buildFailureResult("System catalog manager is not initialized.");
    }

    const std::string &dbName = useStmt->getDbName();
    if (dbName.empty()) {
        LogWriter::warning("executor", "UseExecutor", "executeUse", "Database name is empty.");
        return buildFailureResult("Database name cannot be empty.");
    }

    if (!systemCatalogManager->checkDbExists(dbName)) {
        LogWriter::warning("executor", "UseExecutor", "executeUse", "Database does not exist: " + dbName + ".");
        return buildFailureResult("Database does not exist.", dbName);
    }

    executionContext->setCurrentDbName(dbName);
    LogWriter::info("executor", "UseExecutor", "executeUse", "Database switched successfully: " + dbName + ".");
    return buildSuccessResult("Use database succeeded.", dbName);
}
