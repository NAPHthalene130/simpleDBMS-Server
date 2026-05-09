#include "DeleteExecutor.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "Core.h"
#include "dbLog/DbLogManager.h"
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

DeleteExecutor::DeleteExecutor(Core *core, DatabaseManager *databaseManager)
    : StatementExecutor(core), databaseManager(databaseManager)
{
}

ExecutionStatementType DeleteExecutor::getSupportedType() const
{
    return ExecutionStatementType::Delete;
}

ExecutionResult DeleteExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "DeleteExecutor", "execute", "Delete input pointer is invalid.");
        return buildFailureResult("DeleteExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "DeleteExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("DeleteExecutor received mismatched statement type.");
    }

    return executeDelete(static_cast<const DeleteStmt *>(statement), executionContext);
}

ExecutionResult DeleteExecutor::executeDelete(const DeleteStmt *deleteStmt,
                                               ExecutionContext *executionContext)
{
    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = deleteStmt->getTableName();

    if (dbName.empty()) {
        LogWriter::warning("executor", "DeleteExecutor", "executeDelete", "No database selected.");
        return buildFailureResult("No database selected. Use USE <database> first.", dbName, tableName);
    }

    if (tableName.empty()) {
        LogWriter::warning("executor", "DeleteExecutor", "executeDelete", "Table name is empty.");
        return buildFailureResult("DELETE statement requires a non-empty table name.", dbName, tableName);
    }

    try {
        const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
        const auto dbPath = dbRootPath / dbName;

        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("executor",
                               "DeleteExecutor",
                               "executeDelete",
                               "Database does not exist: " + dbName);
            return buildFailureResult("Database does not exist: " + dbName, dbName, tableName);
        }

        const auto metaPath = dbPath / (tableName + ".tdf");
        if (!std::filesystem::exists(metaPath)) {
            LogWriter::warning("executor",
                               "DeleteExecutor",
                               "executeDelete",
                               "Table does not exist: " + dbName + "." + tableName);
            return buildFailureResult("Table does not exist: " + tableName, dbName, tableName);
        }

        auto table = storage::Table::load(dbPath, tableName);
        const auto &schema = table.schema();

        const auto allRows = table.select(std::vector<std::string>{},
                                                std::vector<storage::Table::WhereCondition>{});
        const auto *whereCondition = deleteStmt->getWhereCondition().get();

        std::vector<std::string> keysToDelete;
        for (const auto &row : allRows) {
            if (row.values.size() != schema.columns.size()) {
                continue;
            }

            if (whereCondition != nullptr
                && !evaluateConditionTree(whereCondition, row.values, schema.columns)) {
                continue;
            }

            keysToDelete.push_back(row.values.front());
        }

        // 记录删除日志 -- NAPH130
        if (core != nullptr && core->getDbLogManager() != nullptr && !keysToDelete.empty()) {
            nlohmann::json deleteSnapshot;
            deleteSnapshot["deleted_keys"] = keysToDelete;
            deleteSnapshot["deleted_count"] = static_cast<std::int32_t>(keysToDelete.size());
            core->getDbLogManager()->logDelete(
                dbName, tableName,
                deleteSnapshot.dump(),
                "DELETE FROM " + tableName + " WHERE ..."
            );
        }

        std::int32_t deletedCount = 0;
        for (const auto &key : keysToDelete) {
            if (table.deleteByPrimaryKey(key)) {
                ++deletedCount;
            }
        }

        LogWriter::info("executor",
                        "DeleteExecutor",
                        "executeDelete",
                        std::string("Delete deleted ") + std::to_string(deletedCount)
                            + " rows from " + dbName + "." + tableName);

        ExecutionResult executionResult;
        executionResult.setStatus(ExecutionStatus::Success);
        executionResult.setMessage("Delete succeeded.");
        executionResult.setAffectedRows(deletedCount);
        executionResult.setDbName(dbName);
        executionResult.setTableName(tableName);
        return executionResult;
    } catch (const std::exception &e) {
        LogWriter::error("executor",
                         "DeleteExecutor",
                         "executeDelete",
                         std::string("Delete failed: ") + e.what());
        return buildFailureResult(std::string("Delete failed: ") + e.what(), dbName, tableName);
    }
}

bool DeleteExecutor::evaluateConditionTree(const ConditionNode *conditionNode,
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

bool DeleteExecutor::evaluateLeafCondition(const ConditionNode *conditionNode,
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
                           "DeleteExecutor",
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

bool DeleteExecutor::compareValues(const std::string &leftValue,
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

bool DeleteExecutor::likeMatch(const std::string &text, const std::string &pattern)
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
