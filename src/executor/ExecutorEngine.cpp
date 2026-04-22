#include "ExecutorEngine.h"

#include <exception>
#include <string>

#include "log/LogWriter.h"

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
        LogWriter::warning("executor", "ExecutorEngine", "registerExecutor", "Skip null statement executor.");
        return;
    }

    ExecutionStatementType statementType = statementExecutor->getSupportedType();
    if (statementType == ExecutionStatementType::Unknown || hasExecutor(statementType)) {
        LogWriter::warning("executor",
                           "ExecutorEngine",
                           "registerExecutor",
                           "Skip duplicated or unknown statement executor registration.");
        return;
    }

    statementExecutors.push_back(statementExecutor);
    LogWriter::info("executor",
                    "ExecutorEngine",
                    "registerExecutor",
                    "Registered statement executor successfully.");
}

bool ExecutorEngine::hasExecutor(ExecutionStatementType statementType) const
{
    return findExecutor(statementType) != nullptr;
}

ExecutionResult ExecutorEngine::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "ExecutorEngine", "execute", "Execute input pointer is invalid.");
        return buildFailureResult("Invalid execute input pointer.", executionContext);
    }

    StatementExecutor *statementExecutor = findExecutor(statement->getStmtType());
    if (statementExecutor == nullptr) {
        LogWriter::error("executor", "ExecutorEngine", "execute", "No executor matches current statement type.");
        return buildFailureResult("Unsupported SQL statement type.", executionContext);
    }

    try {
        LogWriter::debug("executor", "ExecutorEngine", "execute", "Dispatching SQL statement to executor.");
        return statementExecutor->execute(statement, executionContext);
    } catch (const std::exception &exception) {
        LogWriter::error("executor",
                         "ExecutorEngine",
                         "execute",
                         std::string("Executor threw standard exception: ") + exception.what());
        return buildFailureResult(std::string("Executor exception: ") + exception.what(), executionContext);
    } catch (...) {
        LogWriter::fatal("executor", "ExecutorEngine", "execute", "Executor threw unknown exception.");
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
