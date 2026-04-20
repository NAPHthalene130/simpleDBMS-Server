#include "ExecutorEngine.h"

#include <exception>
#include <memory>
#include <string>

#include "statementExecutors/CreateDbExecutor.h"
#include "statementExecutors/CreateTableExecutor.h"
#include "statementExecutors/InsertExecutor.h"
#include "statementExecutors/SelectExecutor.h"

ExecutorEngine::ExecutorEngine()
{
    registerExecutor(std::make_shared<CreateDbExecutor>(systemCatalogManager));
    registerExecutor(std::make_shared<CreateTableExecutor>(databaseManager, tableDefManager));
    registerExecutor(std::make_shared<InsertExecutor>(databaseManager, tableDefManager));
    registerExecutor(std::make_shared<SelectExecutor>(databaseManager, tableDefManager));
}

void ExecutorEngine::registerExecutor(const std::shared_ptr<StatementExecutor> &statementExecutor)
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

ExecutionResult ExecutorEngine::execute(const SQLStatement &statement, ExecutionContext &executionContext)
{
    std::shared_ptr<StatementExecutor> statementExecutor = findExecutor(statement.getStmtType());
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

std::shared_ptr<StatementExecutor> ExecutorEngine::findExecutor(ExecutionStatementType statementType) const
{
    for (const std::shared_ptr<StatementExecutor> &statementExecutor : statementExecutors) {
        if (statementExecutor != nullptr && statementExecutor->getSupportedType() == statementType) {
            return statementExecutor;
        }
    }

    return nullptr;
}
