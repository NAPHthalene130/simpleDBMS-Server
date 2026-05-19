#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "models/parser/ConditionNode.h"
#include "models/parser/SelectStmt.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace subquery_eval {

inline std::string toUpperString(const std::string &value)
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

inline storage::Table::CompareOp mapCompareOp(const std::string &opStr)
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

inline bool compareValues(const std::string &leftValue,
                          storage::Table::CompareOp op,
                          const std::string &rightValue)
{
    if (op == storage::Table::CompareOp::LIKE) {
        return storage::likeMatch(leftValue, rightValue);
    }

    if (leftValue.empty() || rightValue.empty()) {
        return false;
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

inline std::string trim(const std::string &value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

inline bool tryResolveColumnValue(const std::string &operand,
                                  const std::vector<std::string> &row,
                                  const std::vector<std::string> &columns,
                                  const std::string &currentTableName,
                                  std::string &outValue)
{
    auto exactIt = std::find(columns.begin(), columns.end(), operand);
    if (exactIt != columns.end()) {
        const std::size_t index = static_cast<std::size_t>(std::distance(columns.begin(), exactIt));
        if (index < row.size()) {
            outValue = row[index];
            return true;
        }
    }

    const auto dotPos = operand.find('.');
    if (dotPos != std::string::npos) {
        const std::string qualifier = operand.substr(0, dotPos);
        if (currentTableName.empty() || qualifier != currentTableName) {
            return false;
        }
        const std::string columnName = operand.substr(dotPos + 1);
        auto shortIt = std::find(columns.begin(), columns.end(), columnName);
        if (shortIt != columns.end()) {
            const std::size_t index = static_cast<std::size_t>(std::distance(columns.begin(), shortIt));
            if (index < row.size()) {
                outValue = row[index];
                return true;
            }
        }
    }

    return false;
}

inline bool evaluateConditionTree(const ConditionNode *node,
                                  const std::vector<std::string> &row,
                                  const std::vector<std::string> &columns,
                                  const std::string &dbName,
                                  const std::string &currentTableName,
                                  const std::vector<std::string> &outerRow,
                                  const std::vector<std::string> &outerColumns,
                                  const std::string &outerTableName);

inline std::vector<std::string> evaluateSubquery(const SQLStatement *subquery,
                                                 const std::string &dbName,
                                                 const std::string &outerTableName,
                                                 const std::vector<std::string> &outerRow,
                                                 const std::vector<std::string> &outerColumns)
{
    if (subquery == nullptr || dbName.empty()) {
        return {};
    }

    if (subquery->getStmtType() != ExecutionStatementType::Select) {
        return {};
    }

    const auto *selectStmt = static_cast<const SelectStmt *>(subquery);
    const std::string tableName = selectStmt->getTableName();
    if (tableName.empty()) {
        return {};
    }

    try {
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        auto table = storage::Table::load(dbPath, tableName);
        const auto &schema = table.schema();
        const auto allRows = table.select(std::vector<std::string> {},
                                          std::vector<storage::Table::WhereCondition> {});

        std::vector<std::string> result;
        result.reserve(allRows.size());

        for (const auto &dataRow : allRows) {
            if (dataRow.values.size() != schema.columns.size()) {
                continue;
            }

            if (!evaluateConditionTree(selectStmt->getWhereCondition().get(),
                                       dataRow.values,
                                       schema.columns,
                                       dbName,
                                       tableName,
                                       outerRow,
                                       outerColumns,
                                       outerTableName)) {
                continue;
            }

            if (selectStmt->getSelectAllFields() || selectStmt->getTargetFields().empty()) {
                result.push_back(dataRow.values.empty() ? "" : dataRow.values.front());
                continue;
            }

            std::string projectedValue;
            const std::string &targetField = selectStmt->getTargetFields().front();
            if (tryResolveColumnValue(targetField,
                                      dataRow.values,
                                      schema.columns,
                                      tableName,
                                      projectedValue)) {
                result.push_back(projectedValue);
            } else {
                result.push_back(targetField);
            }
        }

        return result;
    } catch (...) {
        return {};
    }
}

inline bool evaluateLeafCondition(const ConditionNode *node,
                                  const std::vector<std::string> &row,
                                  const std::vector<std::string> &columns,
                                  const std::string &dbName,
                                  const std::string &currentTableName,
                                  const std::vector<std::string> &outerRow,
                                  const std::vector<std::string> &outerColumns,
                                  const std::string &outerTableName)
{
    if (node == nullptr) {
        return true;
    }

    if (node->hasSubquery()) {
        const auto subqueryResult = evaluateSubquery(node->getSubquery().get(),
                                                     dbName,
                                                     currentTableName,
                                                     row,
                                                     columns);
        const std::string opUpper = toUpperString(node->getOperator());

        if (opUpper == "EXISTS") {
            return node->isNegated() ? subqueryResult.empty() : !subqueryResult.empty();
        }

        std::string leftValue;
        if (!tryResolveColumnValue(node->getLeftOperand(),
                                   row,
                                   columns,
                                   currentTableName,
                                   leftValue)) {
            return false;
        }

        if (opUpper == "IN" || opUpper == "=") {
            const bool found = std::find(subqueryResult.begin(), subqueryResult.end(), leftValue)
                               != subqueryResult.end();
            return node->isNegated() ? !found : found;
        }

        if (opUpper == "!=" || opUpper == "<>" || opUpper == ">"
            || opUpper == ">=" || opUpper == "<" || opUpper == "<=") {
            if (subqueryResult.empty()) {
                return false;
            }
            const bool result =
                compareValues(leftValue, mapCompareOp(node->getOperator()), subqueryResult.front());
            return node->isNegated() ? !result : result;
        }

        return false;
    }

    std::string leftValue;
    if (!tryResolveColumnValue(node->getLeftOperand(),
                               row,
                               columns,
                               currentTableName,
                               leftValue)) {
        return false;
    }

    const std::string opUpper = toUpperString(node->getOperator());
    bool result = false;

    if (opUpper == "IS NULL") {
        result = leftValue.empty();
    } else if (opUpper == "IS NOT NULL") {
        result = !leftValue.empty();
    } else if (opUpper == "BETWEEN" || opUpper == "NOT BETWEEN") {
        const std::string range = node->getRightOperand();
        const auto andPos = range.find(" AND ");
        if (andPos == std::string::npos) {
            return false;
        }
        const std::string lowValue = trim(range.substr(0, andPos));
        const std::string highValue = trim(range.substr(andPos + 5));
        const bool inRange = compareValues(leftValue, storage::Table::CompareOp::GE, lowValue)
                             && compareValues(leftValue, storage::Table::CompareOp::LE, highValue);
        result = (opUpper == "BETWEEN") ? inRange : !inRange;
    } else if (opUpper == "IN" || opUpper == "NOT IN") {
        std::string inner = trim(node->getRightOperand());
        if (!inner.empty() && inner.front() == '(') {
            inner.erase(inner.begin());
        }
        if (!inner.empty() && inner.back() == ')') {
            inner.pop_back();
        }

        const auto items = storage::split(inner, ',');
        bool found = false;
        for (const auto &item : items) {
            if (compareValues(leftValue, storage::Table::CompareOp::EQ, trim(item))) {
                found = true;
                break;
            }
        }
        result = (opUpper == "IN") ? found : !found;
    } else {
        std::string rightValue = node->getRightOperand();
        std::string resolvedValue;
        if (tryResolveColumnValue(rightValue,
                                  row,
                                  columns,
                                  currentTableName,
                                  resolvedValue)
            || tryResolveColumnValue(rightValue,
                                     outerRow,
                                     outerColumns,
                                     outerTableName,
                                     resolvedValue)) {
            rightValue = resolvedValue;
        }

        const storage::Table::CompareOp compareOp =
            (opUpper == "LIKE" || opUpper == "NOT LIKE")
                ? storage::Table::CompareOp::LIKE
                : mapCompareOp(node->getOperator());

        result = compareValues(leftValue, compareOp, rightValue);
        if (opUpper == "NOT LIKE") {
            result = !result;
        }
    }

    if (node->isNegated()
        && opUpper != "EXISTS"
        && opUpper != "IN"
        && opUpper != "NOT IN"
        && opUpper != "BETWEEN"
        && opUpper != "NOT BETWEEN"
        && opUpper != "LIKE"
        && opUpper != "NOT LIKE"
        && opUpper != "IS NULL"
        && opUpper != "IS NOT NULL") {
        result = !result;
    }

    return result;
}

inline bool evaluateConditionTree(const ConditionNode *node,
                                  const std::vector<std::string> &row,
                                  const std::vector<std::string> &columns,
                                  const std::string &dbName,
                                  const std::string &currentTableName,
                                  const std::vector<std::string> &outerRow,
                                  const std::vector<std::string> &outerColumns,
                                  const std::string &outerTableName)
{
    if (node == nullptr) {
        return true;
    }

    if (node->hasSubquery()) {
        return evaluateLeafCondition(node,
                                     row,
                                     columns,
                                     dbName,
                                     currentTableName,
                                     outerRow,
                                     outerColumns,
                                     outerTableName);
    }

    const auto &leftNode = node->getLeftNode();
    const auto &rightNode = node->getRightNode();
    if (leftNode != nullptr || rightNode != nullptr) {
        const bool leftResult = evaluateConditionTree(leftNode.get(),
                                                      row,
                                                      columns,
                                                      dbName,
                                                      currentTableName,
                                                      outerRow,
                                                      outerColumns,
                                                      outerTableName);
        const bool rightResult = evaluateConditionTree(rightNode.get(),
                                                       row,
                                                       columns,
                                                       dbName,
                                                       currentTableName,
                                                       outerRow,
                                                       outerColumns,
                                                       outerTableName);
        return toUpperString(node->getOperator()) == "AND"
            ? (leftResult && rightResult)
            : (leftResult || rightResult);
    }

    return evaluateLeafCondition(node,
                                 row,
                                 columns,
                                 dbName,
                                 currentTableName,
                                 outerRow,
                                 outerColumns,
                                 outerTableName);
}

} // namespace subquery_eval
