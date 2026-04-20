#include "InsertExecutor.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}
}

InsertExecutor::InsertExecutor(DatabaseManager &databaseManager, TableDefManager &tableDefManager)
    : databaseManager(databaseManager), tableDefManager(tableDefManager)
{
}

ExecutionStatementType InsertExecutor::getSupportedType() const
{
    return ExecutionStatementType::Insert;
}

ExecutionResult InsertExecutor::execute(const SQLStatement &statement, ExecutionContext &executionContext)
{
    if (statement.getStmtType() != getSupportedType()) {
        return buildFailureResult("InsertExecutor received mismatched statement type.");
    }

    return executeInsert(static_cast<const InsertStmt &>(statement), executionContext);
}

ExecutionResult InsertExecutor::executeInsert(const InsertStmt &insertStmt, ExecutionContext &executionContext)
{
    (void)executionContext;
    if (!validateInsertStmt(insertStmt)) {
        return buildFailureResult("Insert statement columns and values do not match.");
    }

    return buildFailureResult("InsertExecutor is registered, but execution logic is not implemented yet.");
}

bool InsertExecutor::validateInsertStmt(const InsertStmt &insertStmt) const
{
    return insertStmt.getColumnNames().empty()
           || insertStmt.getColumnNames().size() == insertStmt.getValues().size();
}
