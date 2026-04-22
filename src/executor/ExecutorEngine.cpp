#include "ExecutorEngine.h"

#include <exception>
#include <string>

namespace {
ExecutionResult buildFailureResult(const std::string &message, const ExecutionContext *executionContext)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    if (executionContext != nullptr) {
        executionResult.setDbName(executionContext->getCurrentDbName());
    }
    return executionResult;
}
}

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
        return buildFailureResult("Invalid execute input pointer.", executionContext);
    }

    StatementExecutor *statementExecutor = findExecutor(statement->getStmtType());
    if (statementExecutor == nullptr) {
        return buildFailureResult("Unsupported SQL statement type.", executionContext);
    }

    try {
        return statementExecutor->execute(statement, executionContext);
    } catch (const std::exception &exception) {
        return buildFailureResult(std::string("Executor exception: ") + exception.what(), executionContext);
    } catch (...) {
        return buildFailureResult("Unknown executor exception.", executionContext);
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
