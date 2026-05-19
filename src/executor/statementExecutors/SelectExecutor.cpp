#include "SelectExecutor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>

#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"

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

ExecutionResult buildSuccessResult(const std::string &message,
                                   const std::vector<std::vector<std::string>> &resultSet,
                                   const std::vector<std::string> &columns,
                                   const std::string &dbName = "",
                                   const std::string &tableName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setResultSet(resultSet);
    executionResult.setColumns(columns);
    executionResult.setDbName(dbName);
    executionResult.setTableName(tableName);
    executionResult.setAffectedRows(static_cast<std::int32_t>(resultSet.size()));
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

storage::Table::CompareOp mapCompareOp(const std::string &opStr)
{
    if (opStr == "=") {
        return storage::Table::CompareOp::EQ;
    }
    if (opStr == "<>" || opStr == "!=") {
        return storage::Table::CompareOp::NE;
    }
    if (opStr == ">") {
        return storage::Table::CompareOp::GT;
    }
    if (opStr == ">=") {
        return storage::Table::CompareOp::GE;
    }
    if (opStr == "<") {
        return storage::Table::CompareOp::LT;
    }
    if (opStr == "<=") {
        return storage::Table::CompareOp::LE;
    }
    return storage::Table::CompareOp::EQ;
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
        LogWriter::error("executor", "SelectExecutor", "execute", "Select input pointer is invalid.");
        return buildFailureResult("SelectExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "SelectExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("SelectExecutor received mismatched statement type.");
    }

    return executeSelect(static_cast<const SelectStmt *>(statement), executionContext);
}

ExecutionResult SelectExecutor::executeSelect(const SelectStmt *selectStmt, ExecutionContext *executionContext)
{
    if (selectStmt == nullptr || executionContext == nullptr) {
        return buildFailureResult("SelectExecutor received null input pointer.");
    }

    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = selectStmt->getTableName();

    if (!validateTargetFields(selectStmt)) {
        LogWriter::warning("executor",
                           "SelectExecutor",
                           "executeSelect",
                           "Select statement target fields are invalid.");
        return buildFailureResult("Select statement target fields are invalid.", dbName, tableName);
    }

    if (isShowDatabaseQuery(selectStmt) || isShowTablesQuery(selectStmt)) {
        return handleMetadataSelect(selectStmt, executionContext);
    }

    return executeTableSelect(selectStmt, executionContext);
}

ExecutionResult SelectExecutor::handleMetadataSelect(const SelectStmt *selectStmt,
                                                      ExecutionContext *executionContext)
{
    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = selectStmt->getTableName();

    if (!validateMetadataFields(selectStmt)) {
        LogWriter::warning("executor",
                           "SelectExecutor",
                           "handleMetadataSelect",
                           "Metadata select target fields are invalid.");
        return buildFailureResult("Select statement target fields are invalid for metadata query.",
                                  dbName,
                                  tableName);
    }

    if (selectStmt->getWhereCondition() != nullptr) {
        LogWriter::warning("executor",
                           "SelectExecutor",
                           "handleMetadataSelect",
                           "WHERE clause is not supported by metadata queries.");
        return buildFailureResult("WHERE clause is not supported by metadata queries.",
                                  dbName,
                                  tableName);
    }

    if (isShowDatabaseQuery(selectStmt)) {
        if (systemCatalogManager == nullptr) {
            LogWriter::error("executor",
                             "SelectExecutor",
                             "handleMetadataSelect",
                             "System catalog manager is not initialized.");
            return buildFailureResult("System catalog manager is not initialized.", dbName, tableName);
        }

        const std::vector<std::string> columns = {"DatabaseName"};
        LogWriter::info("executor",
                        "SelectExecutor",
                        "handleMetadataSelect",
                        "Show databases query executed successfully.");
        return buildSuccessResult("Show databases succeeded.",
                                  buildMetadataResultSet(selectStmt),
                                  columns,
                                  dbName,
                                  tableName);
    }

    if (isShowTablesQuery(selectStmt)) {
        if (dbName.empty()) {
            LogWriter::warning("executor",
                               "SelectExecutor",
                               "handleMetadataSelect",
                               "Show tables query has no active database.");
            return buildFailureResult("No database is selected.", dbName, tableName);
        }

        if (databaseManager == nullptr) {
            LogWriter::error("executor",
                             "SelectExecutor",
                             "handleMetadataSelect",
                             "Database manager is not initialized.");
            return buildFailureResult("Database manager is not initialized.", dbName, tableName);
        }

        const std::vector<std::string> columns = {"TableName"};
        LogWriter::info("executor",
                        "SelectExecutor",
                        "handleMetadataSelect",
                        "Show tables query executed successfully in database " + dbName + ".");
        return buildSuccessResult("Show tables succeeded.",
                                  buildMetadataResultSet(selectStmt),
                                  columns,
                                  dbName,
                                  tableName);
    }

    return buildFailureResult("Unsupported metadata query.", dbName, tableName);
}

ExecutionResult SelectExecutor::executeTableSelect(const SelectStmt *selectStmt,
                                                    ExecutionContext *executionContext)
{
    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = selectStmt->getTableName();

    if (dbName.empty()) {
        LogWriter::warning("executor", "SelectExecutor", "executeTableSelect", "No database selected.");
        return buildFailureResult("No database selected. Use USE <database> first.", dbName, tableName);
    }

    if (tableName.empty()) {
        LogWriter::warning("executor", "SelectExecutor", "executeTableSelect", "Table name is empty.");
        return buildFailureResult("Table name cannot be empty.", dbName, tableName);
    }

    try {
        const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
        const auto dbPath = dbRootPath / dbName;

        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("executor",
                               "SelectExecutor",
                               "executeTableSelect",
                               "Database does not exist: " + dbName);
            return buildFailureResult("Database does not exist: " + dbName, dbName, tableName);
        }

        const auto metaPath = dbPath / (tableName + ".tdf");
        if (!std::filesystem::exists(metaPath)) {
            LogWriter::warning("executor",
                               "SelectExecutor",
                               "executeTableSelect",
                               "Table does not exist: " + dbName + "." + tableName);
            return buildFailureResult("Table does not exist: " + tableName, dbName, tableName);
        }

        auto table = storage::Table::load(dbPath, tableName);
        const auto &schema = table.schema();

        std::vector<std::string> projectedColumns;
        std::vector<std::size_t> projectedIndexes;
        const bool selectAll = selectStmt->getSelectAllFields();

        if (selectAll || selectStmt->getTargetFields().empty()) {
            projectedColumns = schema.columns;
            projectedIndexes.resize(schema.columns.size());
            for (std::size_t i = 0; i < schema.columns.size(); ++i) {
                projectedIndexes[i] = i;
            }
        } else {
            for (const auto &field : selectStmt->getTargetFields()) {
                auto it = std::find(schema.columns.begin(), schema.columns.end(), field);
                if (it == schema.columns.end()) {
                    LogWriter::warning("executor",
                                       "SelectExecutor",
                                       "executeTableSelect",
                                       "Unknown column: " + field);
                    return buildFailureResult("Unknown column: " + field, dbName, tableName);
                }
                projectedColumns.push_back(field);
                projectedIndexes.push_back(
                    static_cast<std::size_t>(std::distance(schema.columns.begin(), it)));
            }
        }

        const auto allRows = table.select(std::vector<std::string>{},
                                                std::vector<storage::Table::WhereCondition>{});
        const auto *whereCondition = selectStmt->getWhereCondition().get();
        const auto *havingCondition = selectStmt->getHavingCondition().get();

        std::vector<std::vector<std::string>> filteredRows;
        for (const auto &row : allRows) {
            if (row.values.size() != schema.columns.size()) {
                continue;
            }

            if (whereCondition != nullptr
                && !evaluateConditionTree(whereCondition, row.values, schema.columns)) {
                continue;
            }

            filteredRows.push_back(row.values);
        }

        const std::vector<std::string> &groupByColumns = selectStmt->getGroupByColumns();
        std::vector<std::vector<std::string>> groupedRows;
        if (!groupByColumns.empty()) {
            std::vector<std::size_t> groupByIndexes;
            groupByIndexes.reserve(groupByColumns.size());
            for (const auto &groupCol : groupByColumns) {
                auto it = std::find(schema.columns.begin(), schema.columns.end(), groupCol);
                if (it == schema.columns.end()) {
                    LogWriter::warning("executor",
                                       "SelectExecutor",
                                       "executeTableSelect",
                                       "Unknown column in GROUP BY: " + groupCol);
                    return buildFailureResult("Unknown column in GROUP BY: " + groupCol,
                                              dbName,
                                              tableName);
                }
                groupByIndexes.push_back(
                    static_cast<std::size_t>(std::distance(schema.columns.begin(), it)));
            }

            std::map<std::string, std::vector<std::string>> groupMap;
            for (const auto &row : filteredRows) {
                std::string groupKey;
                for (const auto idx : groupByIndexes) {
                    if (!groupKey.empty()) {
                        groupKey += "\x1F";
                    }
                    groupKey += row[idx];
                }
                if (groupMap.find(groupKey) == groupMap.end()) {
                    groupMap[groupKey] = row;
                }
            }

            for (auto &entry : groupMap) {
                groupedRows.push_back(std::move(entry.second));
            }
        } else {
            groupedRows = std::move(filteredRows);
        }

        if (havingCondition != nullptr) {
            std::vector<std::vector<std::string>> havingFilteredRows;
            for (const auto &row : groupedRows) {
                if (evaluateConditionTree(havingCondition, row, schema.columns)) {
                    havingFilteredRows.push_back(row);
                }
            }
            groupedRows = std::move(havingFilteredRows);
        }

        std::vector<std::vector<std::string>> resultSet;
        for (const auto &row : groupedRows) {
            std::vector<std::string> projectedRow;
            projectedRow.reserve(projectedIndexes.size());
            for (const auto idx : projectedIndexes) {
                projectedRow.push_back(row[idx]);
            }
            resultSet.push_back(std::move(projectedRow));
        }

        LogWriter::info("executor",
                        "SelectExecutor",
                        "executeTableSelect",
                        std::string("Select returned ") + std::to_string(resultSet.size())
                            + " rows from " + dbName + "." + tableName);
        return buildSuccessResult("Select succeeded.", resultSet, projectedColumns, dbName, tableName);
    } catch (const std::exception &e) {
        LogWriter::error("executor",
                         "SelectExecutor",
                         "executeTableSelect",
                         std::string("Select failed: ") + e.what());
        return buildFailureResult(std::string("Select failed: ") + e.what(), dbName, tableName);
    }
}

bool SelectExecutor::validateTargetFields(const SelectStmt *selectStmt) const
{
    if (selectStmt == nullptr) {
        return false;
    }

    return selectStmt->getSelectAllFields() || !selectStmt->getTargetFields().empty();
}

bool SelectExecutor::evaluateConditionTree(const ConditionNode *conditionNode,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns) const
{
    if (conditionNode == nullptr) {
        return true;
    }

    // 子查询条件委托给 evaluateLeafCondition
    // 作者：NAPH130
    if (conditionNode->hasSubquery()) {
        return evaluateLeafCondition(conditionNode, row, columns);
    }

    const auto &leftNode = conditionNode->getLeftNode();
    const auto &rightNode = conditionNode->getRightNode();

    if (leftNode != nullptr || rightNode != nullptr) {
        const bool leftResult = evaluateConditionTree(leftNode.get(), row, columns);
        const bool rightResult = evaluateConditionTree(rightNode.get(), row, columns);
        const std::string opUpper = toUpperString(conditionNode->getOperator());
        if (opUpper == "AND") {
            return leftResult && rightResult;
        }
        return leftResult || rightResult;
    }

    return evaluateLeafCondition(conditionNode, row, columns);
}

bool SelectExecutor::evaluateLeafCondition(const ConditionNode *conditionNode,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns) const
{
    if (conditionNode == nullptr) {
        return true;
    }

    // 子查询条件（EXISTS / IN）
    // 作者：NAPH130
    if (conditionNode->hasSubquery()) {
        const std::string opUpper = toUpperString(conditionNode->getOperator());
        // EXISTS：若子查询返回行则为 true
        if (opUpper == "EXISTS") {
            // SelectExecutor 路径下不支持全量子查询求值，保守返回 true
            // 主路径应由 PlanExecutor 处理
            return !conditionNode->isNegated();
        }
        if (opUpper == "IN") {
            return !conditionNode->isNegated();
        }
        return false;
    }

    const std::string &columnName = conditionNode->getLeftOperand();
    const std::string &opStr = conditionNode->getOperator();
    const std::string &value = conditionNode->getRightOperand();

    auto it = std::find(columns.begin(), columns.end(), columnName);
    if (it == columns.end()) {
        LogWriter::warning("executor",
                           "SelectExecutor",
                           "evaluateLeafCondition",
                           "Unknown column in WHERE: " + columnName);
        return false;
    }

    const std::size_t columnIndex = static_cast<std::size_t>(std::distance(columns.begin(), it));
    if (columnIndex >= row.size()) {
        return false;
    }

    return compareValues(row[columnIndex], mapCompareOp(opStr), value);
}

std::vector<std::vector<std::string>> SelectExecutor::buildMetadataResultSet(
    const SelectStmt *selectStmt) const
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

bool SelectExecutor::compareValues(const std::string &leftValue,
                                   storage::Table::CompareOp op,
                                   const std::string &rightValue)
{
    if (op == storage::Table::CompareOp::LIKE) {
        return likeMatch(leftValue, rightValue);
    }

    double leftNum = 0.0;
    double rightNum = 0.0;
    bool leftIsNum = false;
    bool rightIsNum = false;

    try {
        std::size_t pos = 0;
        leftNum = std::stod(leftValue, &pos);
        leftIsNum = (pos == leftValue.size());
    } catch (...) {
        leftIsNum = false;
    }

    try {
        std::size_t pos = 0;
        rightNum = std::stod(rightValue, &pos);
        rightIsNum = (pos == rightValue.size());
    } catch (...) {
        rightIsNum = false;
    }

    if (leftIsNum && rightIsNum) {
        switch (op) {
        case storage::Table::CompareOp::EQ:
            return leftNum == rightNum;
        case storage::Table::CompareOp::NE:
            return leftNum != rightNum;
        case storage::Table::CompareOp::GT:
            return leftNum > rightNum;
        case storage::Table::CompareOp::GE:
            return leftNum >= rightNum;
        case storage::Table::CompareOp::LT:
            return leftNum < rightNum;
        case storage::Table::CompareOp::LE:
            return leftNum <= rightNum;
        default:
            return false;
        }
    }

    switch (op) {
    case storage::Table::CompareOp::EQ:
        return leftValue == rightValue;
    case storage::Table::CompareOp::NE:
        return leftValue != rightValue;
    case storage::Table::CompareOp::GT:
        return leftValue > rightValue;
    case storage::Table::CompareOp::GE:
        return leftValue >= rightValue;
    case storage::Table::CompareOp::LT:
        return leftValue < rightValue;
    case storage::Table::CompareOp::LE:
        return leftValue <= rightValue;
    default:
        return false;
    }
}

bool SelectExecutor::likeMatch(const std::string &text, const std::string &pattern)
{
    std::size_t textPos = 0;
    std::size_t patternPos = 0;
    std::size_t starPos = std::string::npos;
    std::size_t matchPos = 0;

    while (textPos < text.size()) {
        if (patternPos < pattern.size()
            && (pattern[patternPos] == text[textPos] || pattern[patternPos] == '_')) {
            ++textPos;
            ++patternPos;
            continue;
        }

        if (patternPos < pattern.size() && pattern[patternPos] == '%') {
            starPos = patternPos;
            ++patternPos;
            matchPos = textPos;
            continue;
        }

        if (starPos != std::string::npos) {
            patternPos = starPos + 1;
            ++matchPos;
            textPos = matchPos;
            continue;
        }

        return false;
    }

    while (patternPos < pattern.size() && pattern[patternPos] == '%') {
        ++patternPos;
    }

    return patternPos == pattern.size();
}
