#include "CreateDbExecutor.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>

#include "log/LogWriter.h"

namespace {
ExecutionResult buildFailureResult(const std::string &message, const std::string &dbName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}

ExecutionResult buildSuccessResult(const std::string &message, const std::string &dbName = "")
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Success);
    executionResult.setMessage(message);
    executionResult.setDbName(dbName);
    return executionResult;
}

std::array<char, 128> toNameArray(const std::string &value)
{
    std::array<char, 128> target{};
    const std::size_t copySize = std::min(value.size(), target.size() - 1);
    std::copy_n(value.data(), copySize, target.begin());
    target[copySize] = '\0';
    return target;
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

CreateDbExecutor::CreateDbExecutor(Core *core, SystemCatalogManager *systemCatalogManager)
    : StatementExecutor(core), systemCatalogManager(systemCatalogManager)
{
}

ExecutionStatementType CreateDbExecutor::getSupportedType() const
{
    return ExecutionStatementType::CreateDatabase;
}

ExecutionResult CreateDbExecutor::execute(const SQLStatement *statement, ExecutionContext *executionContext)
{
    if (statement == nullptr || executionContext == nullptr) {
        LogWriter::error("executor", "CreateDbExecutor", "execute", "Create database input pointer is invalid.");
        return buildFailureResult("CreateDbExecutor received null input pointer.");
    }

    if (statement->getStmtType() != getSupportedType()) {
        LogWriter::error("executor", "CreateDbExecutor", "execute", "Received mismatched statement type.");
        return buildFailureResult("CreateDbExecutor received mismatched statement type.");
    }

    return executeCreateDb(static_cast<const CreateDbStmt *>(statement), executionContext);
}

ExecutionResult CreateDbExecutor::executeCreateDb(const CreateDbStmt *createDbStmt,
                                                  ExecutionContext *executionContext)
{
    (void)executionContext;
    if (createDbStmt == nullptr) {
        return buildFailureResult("Create database statement is null.");
    }

    if (systemCatalogManager == nullptr) {
        return buildFailureResult("System catalog manager is not initialized.");
    }

    const std::string &dbName = createDbStmt->getDbName();
    if (dbName.empty()) {
        LogWriter::warning("executor", "CreateDbExecutor", "executeCreateDb", "Database name is empty.");
        return buildFailureResult("Database name cannot be empty.", dbName);
    }

    if (dbName.size() >= 128) {
        LogWriter::warning("executor", "CreateDbExecutor", "executeCreateDb", "Database name is too long.");
        return buildFailureResult("Database name exceeds the maximum length.", dbName);
    }

    if (systemCatalogManager->checkDbExists(dbName)) {
        LogWriter::warning("executor",
                           "CreateDbExecutor",
                           "executeCreateDb",
                           "Database already exists: " + dbName + ".");
        return buildFailureResult("Database already exists.", dbName);
    }

    const DatabaseBlock databaseBlock = buildDatabaseBlock(createDbStmt);
    if (!systemCatalogManager->createDatabase(databaseBlock)) {
        LogWriter::error("executor",
                         "CreateDbExecutor",
                         "executeCreateDb",
                         "Storage layer failed to create database " + dbName + ".");
        return buildFailureResult("Create database failed in storage layer.", dbName);
    }

    LogWriter::info("executor", "CreateDbExecutor", "executeCreateDb", "Database created successfully: " + dbName + ".");
    return buildSuccessResult("Create database succeeded.", dbName);
}

DatabaseBlock CreateDbExecutor::buildDatabaseBlock(const CreateDbStmt *createDbStmt) const
{
    DatabaseBlock databaseBlock;
    if (createDbStmt == nullptr) {
        return databaseBlock;
    }

    const std::string &dbName = createDbStmt->getDbName();
    databaseBlock.setName(toNameArray(dbName));
    databaseBlock.setType(false);
    databaseBlock.setFileName(toFileNameArray((std::filesystem::path("data") / dbName).string()));
    databaseBlock.setCreateTime(buildCurrentDateTime());
    return databaseBlock;
}
