#include "CreateTableExecutor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <unordered_set>

#include "Core.h"
#include "dbLog/DbLogManager.h"
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
        LogWriter::error("executor", "CreateTableExecutor", "execute", "Create table input pointer is invalid.");
        return buildFailureResult("CreateTableExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "CreateTableExecutor", "execute", "Received mismatched statement type.");
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
        LogWriter::warning("executor", "CreateTableExecutor", "executeCreateTable", "No database is selected.");
        return buildFailureResult("No database is selected.", dbName, tableName);
    }

    if (tableName.empty()) {
        LogWriter::warning("executor", "CreateTableExecutor", "executeCreateTable", "Table name is empty.");
        return buildFailureResult("Table name cannot be empty.", dbName, tableName);
    }

    if (tableName.size() >= 128) {
        LogWriter::warning("executor", "CreateTableExecutor", "executeCreateTable", "Table name is too long.");
        return buildFailureResult("Table name exceeds the maximum length.", dbName, tableName);
    }

    const std::vector<FieldBlock> fieldBlocks = buildFieldBlocks(createTableStmt);
    if (fieldBlocks.empty()) {
        LogWriter::warning("executor",
                           "CreateTableExecutor",
                           "executeCreateTable",
                           "Create table statement does not contain fields.");
        return buildFailureResult("Create table requires at least one field.", dbName, tableName);
    }

    std::unordered_set<std::string> fieldNameSet;
    fieldNameSet.reserve(fieldBlocks.size());
    for (const FieldBlock &fieldBlock : fieldBlocks) {
        const std::string fieldName = fixedArrayToString(fieldBlock.getName());
        if (fieldName.empty()) {
            LogWriter::warning("executor", "CreateTableExecutor", "executeCreateTable", "Field name is empty.");
            return buildFailureResult("Field name cannot be empty.", dbName, tableName);
        }

        if (!fieldNameSet.insert(fieldName).second) {
            LogWriter::warning("executor",
                               "CreateTableExecutor",
                               "executeCreateTable",
                               "Duplicate field name detected in create table statement.");
            return buildFailureResult("Duplicate field name exists in create table statement.", dbName, tableName);
        }
    }

    std::vector<std::string> columnNames;
    std::vector<storage::ColumnMeta> columnMetas;
    columnNames.reserve(fieldBlocks.size());
    columnMetas.reserve(fieldBlocks.size());
    for (const FieldBlock &fieldBlock : fieldBlocks) {
        columnNames.push_back(fixedArrayToString(fieldBlock.getName()));
        storage::ColumnMeta meta;
        meta.integrities = fieldBlock.getIntegrities();
        meta.defaultValue = fieldBlock.getDefaultValue();
        columnMetas.push_back(meta);
    }

    const TableBlock tableBlock = buildTableBlock(createTableStmt, dbName);
    if (!databaseManager->createTable(dbName, tableName, columnNames, columnMetas)) {
        LogWriter::error("executor",
                         "CreateTableExecutor",
                         "executeCreateTable",
                         "Storage layer failed to create table " + tableName + " in database " + dbName + ".");
        return buildFailureResult("Create table failed in storage layer.", dbName, tableName);
    }

    // 记录创建表日志
    if (core != nullptr && core->getDbLogManager() != nullptr) {
        nlohmann::json tableSnapshot;
        tableSnapshot["columns"] = columnNames;
        tableSnapshot["field_count"] = fieldBlocks.size();
        core->getDbLogManager()->logCreateTable(
            dbName, tableName, tableSnapshot.dump(),
            "CREATE TABLE " + tableName + " (...)"
        );
    }

    LogWriter::info("executor",
                    "CreateTableExecutor",
                    "executeCreateTable",
                    "Table created successfully: " + dbName + "." + tableName + ".");
    return buildSuccessResult("Create table succeeded.", dbName, tableName);
}

TableBlock CreateTableExecutor::buildTableBlock(const CreateTableStmt *createTableStmt, const std::string &dbName) const
{
    TableBlock tableBlock;
    if (createTableStmt == nullptr || dbName.empty()) {
        return tableBlock;
    }

    const std::string tableName = fixedArrayToString(createTableStmt->getTableName());
    const DateTime currentDateTime = buildCurrentDateTime();
    const std::filesystem::path dbPath = SystemCatalogManager::getDataRootPath() / dbName;

    tableBlock.setName(createTableStmt->getTableName());
    tableBlock.setRecordNum(0);
    tableBlock.setFieldNum(static_cast<std::int32_t>(buildFieldBlocks(createTableStmt).size()));
    tableBlock.setTdf(toFileNameArray((dbPath / (tableName + ".tdf")).string()));
    tableBlock.setTic(toFileNameArray((dbPath / (tableName + ".tic")).string()));
    tableBlock.setTrd(toFileNameArray((dbPath / (tableName + ".trd")).string()));
    tableBlock.setTid(toFileNameArray((dbPath / (tableName + ".tid")).string()));
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