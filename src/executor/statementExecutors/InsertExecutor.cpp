#include "InsertExecutor.h"

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
        return buildFailureResult("InsertExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("InsertExecutor received mismatched statement type.");
    }

    return executeInsert(static_cast<const InsertStmt *>(statement), executionContext);
}

ExecutionResult InsertExecutor::executeInsert(const InsertStmt *insertStmt, ExecutionContext *executionContext)
{
    const std::string dbName = executionContext != nullptr ? executionContext->getCurrentDbName() : "";
    const std::string tableName = insertStmt != nullptr ? insertStmt->getTableName() : "";

    if (!validateInsertStmt(insertStmt)) {
        return buildFailureResult("Insert statement columns and values do not match.", dbName, tableName);
    }

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
