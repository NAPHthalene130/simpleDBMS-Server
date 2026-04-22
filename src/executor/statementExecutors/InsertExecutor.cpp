#include "InsertExecutor.h"

#include "log/LogWriter.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message,
                                   const std::string &dbName = "",
                                   const std::string &tableName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    executionResult.setTableName(tableName);
    return executionResult;
}
}

InsertExecutor::InsertExecutor(Core *core, DatabaseManager *databaseManager, TableDefManager *tableDefManager)
    : StatementExecutor(core), databaseManager(databaseManager), tableDefManager(tableDefManager)
{
}

ExecutionStatementType InsertExecutor::getSupportedType() const
{
    return ExecutionStatementType::Insert;
}

ExecutionResult InsertExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "InsertExecutor", "execute", "Insert input pointer is invalid.");
        return buildFailureResult("InsertExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "InsertExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("InsertExecutor received mismatched statement type.");
    }

    return executeInsert(static_cast<const InsertStmt *>(statement), executionContext);
}

ExecutionResult InsertExecutor::executeInsert(const InsertStmt *insertStmt, ExecutionContext *executionContext)
{
    const std::string dbName = executionContext != nullptr ? executionContext->getCurrentDbName() : "";
    const std::string tableName = insertStmt != nullptr ? insertStmt->getTableName() : "";

    if (!validateInsertStmt(insertStmt)) {
        LogWriter::warning("executor",
                           "InsertExecutor",
                           "executeInsert",
                           "Insert statement columns and values do not match.");
        return buildFailureResult("Insert statement columns and values do not match.", dbName, tableName);
    }

    LogWriter::warning("executor",
                       "InsertExecutor",
                       "executeInsert",
                       "Insert executor is invoked but storage logic is not implemented yet.");
    return buildFailureResult("InsertExecutor is registered, but execution logic is not implemented yet.",
                              dbName,
                              tableName);
}

bool InsertExecutor::validateInsertStmt(const InsertStmt *insertStmt) const
{
    if (insertStmt == nullptr) {
        return false;
    }

    return insertStmt->getColumnNames().empty()
           || insertStmt->getColumnNames().size() == insertStmt->getValues().size();
}
