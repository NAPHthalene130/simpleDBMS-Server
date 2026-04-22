#include "SelectExecutor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace {
ExecutionResult buildFailureResult(const std::string &message)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    return executionResult;
}

ExecutionResult buildSuccessResult(const std::string &message,
                                   const std::vector<std::vector<std::string>> &resultSet)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setResultSet(resultSet);
    return executionResult;
}

std::string toUpperString(const std::string &value)
{
    std::string normalized = value;
    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return normalized;
}

std::string fixedArrayToString(const std::array<char, 128> &value)
{
    const auto endIterator = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIterator);
}
}

SelectExecutor::SelectExecutor(Core *core,
                               SystemCatalogManager *systemCatalogManager,
                               DatabaseManager *databaseManager,
                               TableDefManager *tableDefManager)
    : StatementExecutor(core),
      systemCatalogManager(systemCatalogManager),
      databaseManager(databaseManager),
      tableDefManager(tableDefManager)
{
}

ExecutionStatementType SelectExecutor::getSupportedType() const
{
    return ExecutionStatementType::Select;
}

ExecutionResult SelectExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        return buildFailureResult("SelectExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        return buildFailureResult("SelectExecutor received mismatched statement type.");
    }

    return executeSelect(static_cast<const SelectStmt *>(statement), executionContext);
}

ExecutionResult SelectExecutor::executeSelect(const SelectStmt *selectStmt, ExecutionContext *executionContext)
{
    if (selectStmt == nullptr || executionContext == nullptr) {
        return buildFailureResult("SelectExecutor received null input pointer.");
    }

    if (!validateTargetFields(selectStmt) || !validateMetadataFields(selectStmt)) {
        return buildFailureResult("Select statement target fields are invalid.");
    }

    if (selectStmt->getWhereCondition() != nullptr && !evaluateCondition(selectStmt->getWhereCondition().get())) {
        return buildFailureResult("WHERE clause is not supported by metadata queries.");
    }

    if (isShowDatabaseQuery(selectStmt)) {
        if (systemCatalogManager == nullptr) {
            return buildFailureResult("System catalog manager is not initialized.");
        }

        return buildSuccessResult("Show databases succeeded.", buildResultSet(selectStmt));
    }

    if (isShowTablesQuery(selectStmt)) {
        if (executionContext->getCurrentDbName().empty()) {
            return buildFailureResult("No database is selected.");
        }

        if (databaseManager == nullptr) {
            return buildFailureResult("Database manager is not initialized.");
        }

        return buildSuccessResult("Show tables succeeded.", buildResultSet(selectStmt));
    }

    return buildFailureResult("Only metadata select queries for databases and tables are supported.");
}

bool SelectExecutor::validateTargetFields(const SelectStmt *selectStmt) const
{
    if (selectStmt == nullptr) {
        return false;
    }

    return selectStmt->getSelectAllFields() || !selectStmt->getTargetFields().empty();
}

bool SelectExecutor::evaluateCondition(const ConditionNode *conditionNode) const
{
    (void)conditionNode;
    return true;
}

std::vector<std::vector<std::string>> SelectExecutor::buildResultSet(const SelectStmt *selectStmt) const
{
    if (selectStmt == nullptr) {
        return {};
    }

    if (isShowDatabaseQuery(selectStmt) && systemCatalogManager != nullptr) {
        std::vector<std::vector<std::string>> resultSet;
        const std::vector<DatabaseBlock> databaseBlocks = systemCatalogManager->getAllDatabases();
        resultSet.reserve(databaseBlocks.size());
        for (const DatabaseBlock &databaseBlock : databaseBlocks) {
            resultSet.push_back({fixedArrayToString(databaseBlock.getName())});
        }
        return resultSet;
    }

    if (isShowTablesQuery(selectStmt) && databaseManager != nullptr) {
        std::vector<std::vector<std::string>> resultSet;
        const std::vector<TableBlock> tableBlocks = databaseManager->getAllTables();
        resultSet.reserve(tableBlocks.size());
        for (const TableBlock &tableBlock : tableBlocks) {
            resultSet.push_back({fixedArrayToString(tableBlock.getName())});
        }
        return resultSet;
    }

    return {};
}

bool SelectExecutor::isShowDatabaseQuery(const SelectStmt *selectStmt) const
{
    if (selectStmt == nullptr) {
        return false;
    }

    const std::string normalizedTableName = toUpperString(selectStmt->getTableName());
    return normalizedTableName == "DATABASE" || normalizedTableName == "DATABASES";
}

bool SelectExecutor::isShowTablesQuery(const SelectStmt *selectStmt) const
{
    if (selectStmt == nullptr) {
        return false;
    }

    const std::string normalizedTableName = toUpperString(selectStmt->getTableName());
    return normalizedTableName == "TABLE" || normalizedTableName == "TABLES";
}

bool SelectExecutor::validateMetadataFields(const SelectStmt *selectStmt) const
{
    if (selectStmt == nullptr) {
        return false;
    }

    if (selectStmt->getSelectAllFields()) {
        return isShowDatabaseQuery(selectStmt) || isShowTablesQuery(selectStmt);
    }

    if (isShowDatabaseQuery(selectStmt)) {
        for (const std::string &fieldName : selectStmt->getTargetFields()) {
            const std::string normalizedFieldName = toUpperString(fieldName);
            if (normalizedFieldName != "NAME" && normalizedFieldName != "DATABASENAME") {
                return false;
            }
        }
        return !selectStmt->getTargetFields().empty();
    }

    if (isShowTablesQuery(selectStmt)) {
        for (const std::string &fieldName : selectStmt->getTargetFields()) {
            const std::string normalizedFieldName = toUpperString(fieldName);
            if (normalizedFieldName != "NAME" && normalizedFieldName != "TABLENAME") {
                return false;
            }
        }
        return !selectStmt->getTargetFields().empty();
    }

    return false;
}
