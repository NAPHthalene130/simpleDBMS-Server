#include "CreateTableExecutor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <unordered_set>

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
                                   const std::string &dbName = "",
                                   const std::string &tableName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    executionResult.setTableName(tableName);
    return executionResult;
}

std::string fixedArrayToString(const std::array<char, 128> &value)
{
    const auto endIterator = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIterator);
}

std::array<char, 256> toFileNameArray(const std::string &value)
{
    std::array<char, 256> target{};
    const std::size_t copySize = std::min(value.size(), target.size() - 1);
    std::copy_n(value.data(), copySize, target.begin());
    target[copySize] = '\0';
    return target;
}

DateTime buildCurrentDateTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    DateTime dateTime;
    dateTime.setYear(static_cast<std::uint16_t>(localTime.tm_year + 1900));
    dateTime.setMonth(static_cast<std::uint16_t>(localTime.tm_mon + 1));
    dateTime.setDayOfWeek(static_cast<std::uint16_t>(localTime.tm_wday));
    dateTime.setDay(static_cast<std::uint16_t>(localTime.tm_mday));
    dateTime.setHour(static_cast<std::uint16_t>(localTime.tm_hour));
    dateTime.setMinute(static_cast<std::uint16_t>(localTime.tm_min));
    dateTime.setSecond(static_cast<std::uint16_t>(localTime.tm_sec));
    dateTime.setMilliseconds(static_cast<std::uint16_t>(milliseconds.count()));
    return dateTime;
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
    (void)tableDefManager;
    if (createTableStmt == nullptr || executionContext == nullptr) {
        return buildFailureResult("Create table input is invalid.");
    }

    const std::string dbName = executionContext->getCurrentDbName();
    const std::string tableName = fixedArrayToString(createTableStmt->getTableName());

    if (databaseManager == nullptr) {
        return buildFailureResult("Database manager is not initialized.", dbName, tableName);
    }

    if (dbName.empty()) {
        return buildFailureResult("No database is selected.", dbName, tableName);
    }

    if (tableName.empty()) {
        return buildFailureResult("Table name cannot be empty.", dbName, tableName);
    }

    if (tableName.size() >= 128) {
        return buildFailureResult("Table name exceeds the maximum length.", dbName, tableName);
    }

    const std::vector<FieldBlock> fieldBlocks = buildFieldBlocks(createTableStmt);
    if (fieldBlocks.empty()) {
        return buildFailureResult("Create table requires at least one field.", dbName, tableName);
    }

    std::unordered_set<std::string> fieldNameSet;
    fieldNameSet.reserve(fieldBlocks.size());
    for (const FieldBlock &fieldBlock : fieldBlocks) {
        const std::string fieldName = fixedArrayToString(fieldBlock.getName());
        if (fieldName.empty()) {
            return buildFailureResult("Field name cannot be empty.", dbName, tableName);
        }

        if (!fieldNameSet.insert(fieldName).second) {
            return buildFailureResult("Duplicate field name exists in create table statement.", dbName, tableName);
        }
    }

    const TableBlock tableBlock = buildTableBlock(createTableStmt);
    if (!databaseManager->createTable(tableBlock)) {
        return buildFailureResult("Create table failed in storage layer.", dbName, tableName);
    }

    return buildSuccessResult("Create table succeeded.", dbName, tableName);
}

TableBlock CreateTableExecutor::buildTableBlock(const CreateTableStmt *createTableStmt) const
{
    TableBlock tableBlock;
    if (createTableStmt == nullptr) {
        return tableBlock;
    }

    const std::string tableName = fixedArrayToString(createTableStmt->getTableName());
    const DateTime currentDateTime = buildCurrentDateTime();

    tableBlock.setName(createTableStmt->getTableName());
    tableBlock.setRecordNum(0);
    tableBlock.setFieldNum(static_cast<std::int32_t>(buildFieldBlocks(createTableStmt).size()));
    tableBlock.setTdf(toFileNameArray(tableName + ".tdf"));
    tableBlock.setTic(toFileNameArray(tableName + ".tic"));
    tableBlock.setTrd(toFileNameArray(tableName + ".trd"));
    tableBlock.setTid(toFileNameArray(tableName + ".tid"));
    tableBlock.setCreateTime(currentDateTime);
    tableBlock.setModifyTime(currentDateTime);
    return tableBlock;
}

std::vector<FieldBlock> CreateTableExecutor::buildFieldBlocks(const CreateTableStmt *createTableStmt) const
{
    if (createTableStmt == nullptr) {
        return {};
    }

    std::vector<FieldBlock> fieldBlocks = createTableStmt->getFields();
    const DateTime currentDateTime = buildCurrentDateTime();
    for (std::size_t index = 0; index < fieldBlocks.size(); ++index) {
        fieldBlocks[index].setOrder(static_cast<std::int32_t>(index));
        fieldBlocks[index].setModifyTime(currentDateTime);
    }

    return fieldBlocks;
}
