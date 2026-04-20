#include "ExecutorEngine.h"

#include <exception>
#include <string>

ExecutorEngine::ExecutorEngine(Core *core)
    : core(core)
{
}

void ExecutorEngine::registerExecutor(StatementExecutor *statementExecutor)
{
    if (statementExecutor == nullptr) {
        return;
    }

    ExecutionStatementType statementType = statementExecutor->getSupportedType();
    if (statementType == ExecutionStatementType::Unknown || hasExecutor(statementType)) {
        return;
    }

    statementExecutors.push_back(statementExecutor);
}

bool ExecutorEngine::hasExecutor(ExecutionStatementType statementType) const
{
    return findExecutor(statementType) != nullptr;
}

ExecutionResult ExecutorEngine::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        ExecutionResult executionResult;
        executionResult.setStatus(ExecutionStatus::Failure);
        executionResult.setMessage("Invalid execute input pointer.");
        return executionResult;
    }

    StatementExecutor *statementExecutor = findExecutor(statement->getStmtType());
    if (statementExecutor == nullptr) {
        ExecutionResult executionResult;
        executionResult.setStatus(ExecutionStatus::Failure);
        executionResult.setMessage("Unsupported SQL statement type.");
        return executionResult;
    }

    try {
        return statementExecutor->execute(statement, executionContext);
    } catch (const std::exception &exception) {
        ExecutionResult executionResult;
        executionResult.setStatus(ExecutionStatus::Failure);
        executionResult.setMessage(std::string("Executor exception: ") + exception.what());
        return executionResult;
    } catch (...) {
        ExecutionResult executionResult;
        executionResult.setStatus(ExecutionStatus::Failure);
        executionResult.setMessage("Unknown executor exception.");
        return executionResult;
    }
}

StatementExecutor *ExecutorEngine::findExecutor(ExecutionStatementType statementType) const
{
    for (StatementExecutor *statementExecutor : statementExecutors) {
        if (statementExecutor != nullptr && statementExecutor->getSupportedType() == statementType) {
            return statementExecutor;
        }
    }

    return nullptr;
}
