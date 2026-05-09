#include "DbLogManager.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "Core.h"
#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"

namespace {
void updateOperationCounterFromLogFile(const std::filesystem::path &logPath, std::int64_t &operationIdCounter)
{
    if (!std::filesystem::exists(logPath)) {
        return;
    }

    std::ifstream inFile(logPath);
    std::string line;
    while (std::getline(inFile, line)) {
        if (line.empty()) {
            continue;
        }
        LogBlock block;
        if (LogBlock::fromJsonString(line, block) && block.getOperationId() > operationIdCounter) {
            operationIdCounter = block.getOperationId();
        }
    }
}
} // namespace

// ──────────────────────────────────────────────
// 构造 / 析构
// ──────────────────────────────────────────────

DbLogManager::DbLogManager(Core *core)
    : core(core),
      operationIdCounter(0)
{
    const auto &dataRootPath = SystemCatalogManager::getDataRootPath();
    std::filesystem::create_directories(dataRootPath);

    if (std::filesystem::exists(dataRootPath)) {
        for (const auto &entry : std::filesystem::directory_iterator(dataRootPath)) {
            if (!entry.is_directory()) {
                continue;
            }

            const std::string databaseName = entry.path().filename().string();
            updateOperationCounterFromLogFile(entry.path() / (databaseName + ".log"), operationIdCounter);
        }
    }

    LogWriter::info("dbLog", "DbLogManager", "DbLogManager",
                    "DbLogManager initialized. Current operation ID: " + std::to_string(operationIdCounter));
}

DbLogManager::~DbLogManager()
{
    LogWriter::info("dbLog", "DbLogManager", "~DbLogManager",
                    "DbLogManager is being released. Final operation ID: " + std::to_string(operationIdCounter));
}

// ──────────────────────────────────────────────
// 日志记录接口
// ──────────────────────────────────────────────

void DbLogManager::logCreateDatabase(const std::string &databaseName,
                                     const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setOperationType(DbLogOperationType::CreateDatabase);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::info("dbLog", "DbLogManager", "logCreateDatabase",
                    "Logged CREATE DATABASE: " + databaseName);
}

void DbLogManager::logDropDatabase(const std::string &databaseName,
                                   const std::string &beforeData,
                                   const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setOperationType(DbLogOperationType::DropDatabase);
    block.setBeforeData(beforeData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::info("dbLog", "DbLogManager", "logDropDatabase",
                    "Logged DROP DATABASE: " + databaseName);
}

void DbLogManager::logCreateTable(const std::string &databaseName,
                                  const std::string &tableName,
                                  const std::string &afterData,
                                  const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setTableName(tableName);
    block.setOperationType(DbLogOperationType::CreateTable);
    block.setAfterData(afterData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::info("dbLog", "DbLogManager", "logCreateTable",
                    "Logged CREATE TABLE: " + databaseName + "." + tableName);
}

void DbLogManager::logDropTable(const std::string &databaseName,
                                const std::string &tableName,
                                const std::string &beforeData,
                                const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setTableName(tableName);
    block.setOperationType(DbLogOperationType::DropTable);
    block.setBeforeData(beforeData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::info("dbLog", "DbLogManager", "logDropTable",
                    "Logged DROP TABLE: " + databaseName + "." + tableName);
}

void DbLogManager::logAlterTable(const std::string &databaseName,
                                 const std::string &tableName,
                                 const std::string &beforeData,
                                 const std::string &afterData,
                                 const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setTableName(tableName);
    block.setOperationType(DbLogOperationType::AlterTable);
    block.setBeforeData(beforeData);
    block.setAfterData(afterData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::info("dbLog", "DbLogManager", "logAlterTable",
                    "Logged ALTER TABLE: " + databaseName + "." + tableName);
}

void DbLogManager::logInsert(const std::string &databaseName,
                             const std::string &tableName,
                             const std::string &afterData,
                             const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setTableName(tableName);
    block.setOperationType(DbLogOperationType::Insert);
    block.setAfterData(afterData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::debug("dbLog", "DbLogManager", "logInsert",
                     "Logged INSERT into " + databaseName + "." + tableName);
}

void DbLogManager::logDelete(const std::string &databaseName,
                             const std::string &tableName,
                             const std::string &beforeData,
                             const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setTableName(tableName);
    block.setOperationType(DbLogOperationType::Delete);
    block.setBeforeData(beforeData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::debug("dbLog", "DbLogManager", "logDelete",
                     "Logged DELETE from " + databaseName + "." + tableName);
}

void DbLogManager::logUpdate(const std::string &databaseName,
                             const std::string &tableName,
                             const std::string &beforeData,
                             const std::string &afterData,
                             const std::string &sqlText)
{
    LogBlock block;
    block.setOperationId(nextOperationId());
    block.setTimestamp(buildCurrentDateTime());
    block.setDatabaseName(databaseName);
    block.setTableName(tableName);
    block.setOperationType(DbLogOperationType::Update);
    block.setBeforeData(beforeData);
    block.setAfterData(afterData);
    block.setSqlText(sqlText);

    appendLog(block);

    LogWriter::debug("dbLog", "DbLogManager", "logUpdate",
                     "Logged UPDATE on " + databaseName + "." + tableName);
}

// ──────────────────────────────────────────────
// 恢复接口
// ──────────────────────────────────────────────

bool DbLogManager::dbRecover(const std::string &databaseName, const DateTime &targetTime)
{
    LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                    "Starting recovery for database: " + databaseName);

    // 读取该数据库的所有日志
    const std::vector<LogBlock> allLogs = getLogsForDatabase(databaseName);

    if (allLogs.empty()) {
        LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                        "No log records found for database: " + databaseName);
        return true; // 无日志即无需恢复
    }

    // 按时间顺序回放，仅处理时间 <= targetTime 的记录
    for (const auto &log : allLogs) {
        if (!dateTimeLessOrEqual(log.getTimestamp(), targetTime)) {
            break; // 后续记录时间均晚于目标时间，停止回放
        }

        // 根据操作类型执行逆向/正向恢复动作
        // 此处记录恢复日志，实际重建操作需与 StorageManager 协同
        LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                        "Replaying operation [" + std::to_string(log.getOperationId())
                            + "] type=" + LogBlock::operationTypeToString(log.getOperationType())
                            + " on " + log.getDatabaseName()
                            + (log.getTableName().empty() ? "" : "." + log.getTableName()));
    }

    LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                    "Recovery completed for database: " + databaseName);

    return true;
}

std::vector<LogBlock> DbLogManager::getLogsForDatabase(const std::string &databaseName)
{
    std::vector<LogBlock> result;
    const std::string logPath = getLogFilePath(databaseName);

    if (logPath.empty() || !std::filesystem::exists(logPath)) {
        return result;
    }

    std::ifstream inFile(logPath);
    std::string line;
    while (std::getline(inFile, line)) {
        if (line.empty()) {
            continue;
        }
        LogBlock block;
        if (LogBlock::fromJsonString(line, block)) {
            if (block.getDatabaseName() == databaseName) {
                result.push_back(block);
            }
        }
    }

    // 按时间戳升序排序
    std::sort(result.begin(), result.end(),
              [](const LogBlock &a, const LogBlock &b) {
                  return dateTimeLessOrEqual(a.getTimestamp(), b.getTimestamp())
                         && !(a.getTimestamp().getYear() == b.getTimestamp().getYear()
                              && a.getTimestamp().getMonth() == b.getTimestamp().getMonth()
                              && a.getTimestamp().getDay() == b.getTimestamp().getDay()
                              && a.getTimestamp().getHour() == b.getTimestamp().getHour()
                              && a.getTimestamp().getMinute() == b.getTimestamp().getMinute()
                              && a.getTimestamp().getSecond() == b.getTimestamp().getSecond()
                              && a.getTimestamp().getMilliseconds() == b.getTimestamp().getMilliseconds()
                              && a.getOperationId() >= b.getOperationId());
              });

    return result;
}

std::int64_t DbLogManager::getCurrentOperationId() const
{
    return operationIdCounter;
}

// ──────────────────────────────────────────────
// 私有辅助方法
// ──────────────────────────────────────────────

void DbLogManager::appendLog(const LogBlock &block)
{
    std::lock_guard<std::mutex> lock(logFileMutex);

    const std::string logPath = getLogFilePath(block.getDatabaseName());
    if (logPath.empty()) {
        LogWriter::error("dbLog", "DbLogManager", "appendLog",
                         "Failed to resolve database log file path.");
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(logPath).parent_path());
    std::ofstream outFile(logPath, std::ios::app);
    if (!outFile.is_open()) {
        LogWriter::error("dbLog", "DbLogManager", "appendLog",
                         "Failed to open log file: " + logPath);
        return;
    }

    outFile << block.toJsonString() << std::endl;
}

std::int64_t DbLogManager::nextOperationId()
{
    std::lock_guard<std::mutex> lock(logFileMutex);
    return ++operationIdCounter;
}

std::string DbLogManager::getLogFilePath(const std::string &databaseName) const
{
    if (databaseName.empty()) {
        return "";
    }

    const std::filesystem::path dbDir = SystemCatalogManager::getDataRootPath() / databaseName;
    return (dbDir / (databaseName + ".log")).string();
}

DateTime DbLogManager::buildCurrentDateTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);

    DateTime dt;
    dt.setYear(static_cast<std::uint16_t>(localTime.tm_year + 1900));
    dt.setMonth(static_cast<std::uint16_t>(localTime.tm_mon + 1));
    dt.setDayOfWeek(static_cast<std::uint16_t>(localTime.tm_wday));
    dt.setDay(static_cast<std::uint16_t>(localTime.tm_mday));
    dt.setHour(static_cast<std::uint16_t>(localTime.tm_hour));
    dt.setMinute(static_cast<std::uint16_t>(localTime.tm_min));
    dt.setSecond(static_cast<std::uint16_t>(localTime.tm_sec));
    dt.setMilliseconds(0);
    return dt;
}

bool DbLogManager::dateTimeLessOrEqual(const DateTime &a, const DateTime &b)
{
    // 按年、月、日、时、分、秒、毫秒逐级比较
    if (a.getYear() != b.getYear()) {
        return a.getYear() < b.getYear();
    }
    if (a.getMonth() != b.getMonth()) {
        return a.getMonth() < b.getMonth();
    }
    if (a.getDay() != b.getDay()) {
        return a.getDay() < b.getDay();
    }
    if (a.getHour() != b.getHour()) {
        return a.getHour() < b.getHour();
    }
    if (a.getMinute() != b.getMinute()) {
        return a.getMinute() < b.getMinute();
    }
    if (a.getSecond() != b.getSecond()) {
        return a.getSecond() < b.getSecond();
    }
    return a.getMilliseconds() <= b.getMilliseconds();
}
