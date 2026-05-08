#include "UpdateExecutor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"

namespace {

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

} // namespace

UpdateExecutor::UpdateExecutor(Core *core, DatabaseManager *databaseManager)
    : StatementExecutor(core), databaseManager(databaseManager)
{
}

ExecutionStatementType UpdateExecutor::getSupportedType() const
{
    return ExecutionStatementType::Update;
}

ExecutionResult UpdateExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "UpdateExecutor", "execute", "Update input pointer is invalid.");
        return buildFailureResult("UpdateExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "UpdateExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("UpdateExecutor received mismatched statement type.");
    }

    return executeUpdate(static_cast<const UpdateStmt *>(statement), executionContext);
}

ExecutionResult UpdateExecutor::executeUpdate(const UpdateStmt *updateStmt,
                                               ExecutionContext *executionContext)
{
    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = updateStmt->getTableName();

    if (dbName.empty()) {
        LogWriter::warning("executor", "UpdateExecutor", "executeUpdate", "No database selected.");
        return buildFailureResult("No database selected. Use USE <database> first.", dbName, tableName);
    }

    if (tableName.empty()) {
        LogWriter::warning("executor", "UpdateExecutor", "executeUpdate", "Table name is empty.");
        return buildFailureResult("UPDATE statement requires a non-empty table name.", dbName, tableName);
    }

    const std::vector<std::string> &setColumns = updateStmt->getColumnNames();
    const std::vector<std::string> &setValues = updateStmt->getValues();

    if (setColumns.empty() || setColumns.size() != setValues.size()) {
        LogWriter::warning("executor",
                           "UpdateExecutor",
                           "executeUpdate",
                           "UPDATE statement assignments are invalid.");
        return buildFailureResult("UPDATE statement assignments are invalid.", dbName, tableName);
    }

    try {
        const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
        const auto dbPath = dbRootPath / dbName;

        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("executor",
                               "UpdateExecutor",
                               "executeUpdate",
                               "Database does not exist: " + dbName);
            return buildFailureResult("Database does not exist: " + dbName, dbName, tableName);
        }

        const auto metaPath = dbPath / (tableName + ".tdf");
        if (!std::filesystem::exists(metaPath)) {
            LogWriter::warning("executor",
                               "UpdateExecutor",
                               "executeUpdate",
                               "Table does not exist: " + dbName + "." + tableName);
            return buildFailureResult("Table does not exist: " + tableName, dbName, tableName);
        }

        auto table = storage::Table::load(dbPath, tableName);
        const auto &schema = table.schema();

        std::vector<std::size_t> setColumnIndexes;
        setColumnIndexes.reserve(setColumns.size());
        for (const auto &setColumn : setColumns) {
            auto it = std::find(schema.columns.begin(), schema.columns.end(), setColumn);
            if (it == schema.columns.end()) {
                LogWriter::warning("executor",
                                   "UpdateExecutor",
                                   "executeUpdate",
                                   "Unknown column in SET: " + setColumn);
                return buildFailureResult("Unknown column in SET: " + setColumn, dbName, tableName);
            }
            setColumnIndexes.push_back(
                static_cast<std::size_t>(std::distance(schema.columns.begin(), it)));
        }

        const auto allRows = table.select({}, {});
        const auto *whereCondition = updateStmt->getWhereCondition().get();

        struct RowUpdate {
            std::string primaryKey;
            std::vector<std::string> newValues;
        };

        std::vector<RowUpdate> rowsToUpdate;
        for (const auto &row : allRows) {
            if (row.values.size() != schema.columns.size()) {
                continue;
            }

            if (whereCondition != nullptr
                && !evaluateConditionTree(whereCondition, row.values, schema.columns)) {
                continue;
            }

            std::vector<std::string> newValues = row.values;
            for (std::size_t i = 0; i < setColumnIndexes.size(); ++i) {
                newValues[setColumnIndexes[i]] = setValues[i];
            }

            rowsToUpdate.push_back({row.values.front(), std::move(newValues)});
        }

        std::int32_t updatedCount = 0;
        for (auto &rowUpdate : rowsToUpdate) {
            if (table.updateByPrimaryKey(rowUpdate.primaryKey, rowUpdate.newValues)) {
                ++updatedCount;
            }
        }

        LogWriter::info("executor",
                        "UpdateExecutor",
                        "executeUpdate",
                        std::string("Update updated ") + std::to_string(updatedCount)
                            + " rows in " + dbName + "." + tableName);

        ExecutionResult executionResult;
        executionResult.setStatus(ExecutionStatus::Success);
        executionResult.setMessage("Update succeeded.");
        executionResult.setAffectedRows(updatedCount);
        executionResult.setDbName(dbName);
        executionResult.setTableName(tableName);
        return executionResult;
    } catch (const std::exception &e) {
        LogWriter::error("executor",
                         "UpdateExecutor",
                         "executeUpdate",
                         std::string("Update failed: ") + e.what());
        return buildFailureResult(std::string("Update failed: ") + e.what(), dbName, tableName);
    }
}

bool UpdateExecutor::evaluateConditionTree(const ConditionNode *conditionNode,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns)
{
    if (conditionNode == nullptr) {
        return true;
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

bool UpdateExecutor::evaluateLeafCondition(const ConditionNode *conditionNode,
                                           const std::vector<std::string> &row,
                                           const std::vector<std::string> &columns)
{
    if (conditionNode == nullptr) {
        return true;
    }

    const std::string &columnName = conditionNode->getLeftOperand();
    const std::string &opStr = conditionNode->getOperator();
    const std::string &value = conditionNode->getRightOperand();

    auto it = std::find(columns.begin(), columns.end(), columnName);
    if (it == columns.end()) {
        LogWriter::warning("executor",
                           "UpdateExecutor",
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

bool UpdateExecutor::compareValues(const std::string &leftValue,
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

bool UpdateExecutor::likeMatch(const std::string &text, const std::string &pattern)
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
