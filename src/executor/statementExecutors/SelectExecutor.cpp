#include "SelectExecutor.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}
}

SelectExecutor::SelectExecutor(DatabaseManager &databaseManager, TableDefManager &tableDefManager)
    : databaseManager(databaseManager), tableDefManager(tableDefManager)
{
}

ExecutionStatementType SelectExecutor::getSupportedType() const
{
    return ExecutionStatementType::Select;
}

ExecutionResult SelectExecutor::execute(const SQLStatement &statement, ExecutionContext &executionContext)
{
    if (statement.getStmtType() != getSupportedType()) {
        return buildFailureResult("SelectExecutor received mismatched statement type.");
    }

    return executeSelect(static_cast<const SelectStmt &>(statement), executionContext);
}

ExecutionResult SelectExecutor::executeSelect(const SelectStmt &selectStmt, ExecutionContext &executionContext)
{
    (void)executionContext;
    if (!validateTargetFields(selectStmt)) {
        return buildFailureResult("Select statement target fields are invalid.");
    }

    return buildFailureResult("SelectExecutor is registered, but execution logic is not implemented yet.");
}

bool SelectExecutor::validateTargetFields(const SelectStmt &selectStmt) const
{
    return selectStmt.getSelectAllFields() || !selectStmt.getTargetFields().empty();
}

bool SelectExecutor::evaluateCondition(const ConditionNode &conditionNode) const
{
    (void)conditionNode;
    return true;
}

std::vector<std::vector<std::string>> SelectExecutor::buildResultSet(const SelectStmt &selectStmt) const
{
    (void)selectStmt;
    return {};
}
