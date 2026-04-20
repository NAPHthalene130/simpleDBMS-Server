#include "CreateTableExecutor.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}
}

CreateTableExecutor::CreateTableExecutor(Core *core, DatabaseManager *databaseManager, TableDefManager *tableDefManager)
    : StatementExecutor(core), databaseManager(databaseManager), tableDefManager(tableDefManager)
{
}

ExecutionStatementType CreateTableExecutor::getSupportedType() const
{
    return ExecutionStatementType::CreateTable;
}

ExecutionResult CreateTableExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        return buildFailureResult("CreateTableExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("CreateTableExecutor received mismatched statement type.");
    }

    return executeCreateTable(static_cast<const CreateTableStmt *>(statement), executionContext);
}

ExecutionResult CreateTableExecutor::executeCreateTable(const CreateTableStmt *createTableStmt,
                                                        ExecutionContext *executionContext)
{
    (void)createTableStmt;
    (void)executionContext;
    return buildFailureResult("CreateTableExecutor is registered, but execution logic is not implemented yet.");
}

TableBlock CreateTableExecutor::buildTableBlock(const CreateTableStmt *createTableStmt) const
{
    (void)createTableStmt;
    return TableBlock();
}

std::vector<FieldBlock> CreateTableExecutor::buildFieldBlocks(const CreateTableStmt *createTableStmt) const
{
    if (createTableStmt == nullptr) {
        return {};
    }

    return createTableStmt->getFields();
}
