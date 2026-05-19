#include "DbLogManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>

#include "Core.h"
#include "DbLogSnapshotUtils.h"
#include "log/LogWriter.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"

namespace {
template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value)
{
    std::array<char, N> result{};
    const auto copyLen = std::min<std::size_t>(value.size(), N - 1);
    std::memcpy(result.data(), value.data(), copyLen);
    return result;
}

const std::filesystem::path &getDbLogRootPath()
{
    static const std::filesystem::path dbLogRoot =
        (SystemCatalogManager::getDataRootPath() / "_db_logs").lexically_normal();
    return dbLogRoot;
}

std::filesystem::path getLegacyLogFilePath(const std::string &databaseName)
{
    return SystemCatalogManager::getDataRootPath() / databaseName / (databaseName + ".log");
}

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

bool createDatabaseIfMissing(SystemCatalogManager *systemCatalogManager, const std::string &dbName)
{
    if (systemCatalogManager == nullptr || dbName.empty()) {
        return false;
    }

    if (systemCatalogManager->checkDbExists(dbName)) {
        return true;
    }

    DatabaseBlock databaseBlock;
    databaseBlock.setName(stringToArray<128>(dbName));
    databaseBlock.setType(false);
    return systemCatalogManager->createDatabase(databaseBlock);
}

bool restoreTableSnapshot(DatabaseManager *databaseManager,
                          const std::string &dbName,
                          const nlohmann::json &tableSnapshot)
{
    if (databaseManager == nullptr || dbName.empty()) {
        return false;
    }

    const std::string tableName = tableSnapshot.value("table_name", "");
    if (tableName.empty()) {
        return false;
    }

    const std::vector<std::string> columns = dblog_snapshot::parseColumns(tableSnapshot);
    std::vector<storage::ColumnMeta> columnMetas = dblog_snapshot::parseColumnMetas(tableSnapshot);
    const auto rows = dblog_snapshot::parseRows(tableSnapshot);
    if (columns.empty()) {
        return false;
    }
    if (columnMetas.size() < columns.size()) {
        columnMetas.resize(columns.size());
    }

    databaseManager->dropTable(dbName, tableName);
    if (!databaseManager->createTable(dbName, tableName, columns, columnMetas)) {
        return false;
    }

    for (const auto &rowValues : rows) {
        if (!databaseManager->insertRow(dbName, tableName, rowValues)) {
            return false;
        }
    }
    return true;
}

bool restoreDatabaseSnapshot(SystemCatalogManager *systemCatalogManager,
                             DatabaseManager *databaseManager,
                             const std::string &dbName,
                             const nlohmann::json &databaseSnapshot)
{
    if (!createDatabaseIfMissing(systemCatalogManager, dbName)) {
        return false;
    }

    if (!databaseSnapshot.contains("tables") || !databaseSnapshot["tables"].is_array()) {
        return true;
    }

    for (const auto &tableSnapshot : databaseSnapshot["tables"]) {
        if (!restoreTableSnapshot(databaseManager, dbName, tableSnapshot)) {
            return false;
        }
    }
    return true;
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
    std::filesystem::create_directories(getDbLogRootPath());

    if (std::filesystem::exists(dataRootPath)) {
        for (const auto &entry : std::filesystem::directory_iterator(dataRootPath)) {
            if (!entry.is_directory()) {
                continue;
            }

            const std::string databaseName = entry.path().filename().string();
            if (databaseName == "_db_logs") {
                continue;
            }
            updateOperationCounterFromLogFile(getLegacyLogFilePath(databaseName), operationIdCounter);
            updateOperationCounterFromLogFile(getDbLogRootPath() / (databaseName + ".log"), operationIdCounter);
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
                    "Starting point-in-time recovery for database: " + databaseName);

    if (core == nullptr || core->getStorageManager() == nullptr
        || core->getStorageManager()->getDatabaseManager() == nullptr) {
        LogWriter::error("dbLog", "DbLogManager", "dbRecover",
                         "Storage manager is not available for recovery.");
        return false;
    }

    auto *databaseManager = core->getStorageManager()->getDatabaseManager();
    auto *systemCatalogManager = core->getStorageManager()->getSystemCatalogManager();

    // 读取该数据库的所有日志（按时间升序）
    const std::vector<LogBlock> allLogs = getLogsForDatabase(databaseName);

    if (allLogs.empty()) {
        LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                        "No log records found. Recovery not needed.");
        return true;
    }

    // 收集目标时间之后的所有操作（需要回退的操作）
    std::vector<LogBlock> logsToUndo;
    for (const auto &log : allLogs) {
        if (!dateTimeLessOrEqual(log.getTimestamp(), targetTime)) {
            logsToUndo.push_back(log);
        }
    }

    if (logsToUndo.empty()) {
        LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                        "No operations to undo. Database is at or before target time.");
        return true;
    }

    LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                    std::to_string(logsToUndo.size()) + " operations to undo.");

    // 从最新的操作开始逆向回退（栈式撤销）
    std::reverse(logsToUndo.begin(), logsToUndo.end());

    std::int64_t undoneCount = 0;
    bool recoverySucceeded = true;
    for (const auto &log : logsToUndo) {
        try {
            const std::string tbName = log.getTableName();
            const DbLogOperationType opType = log.getOperationType();
            bool undoResult = false;

            LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                            "Undoing operation [" + std::to_string(log.getOperationId())
                                + "] " + LogBlock::operationTypeToString(opType)
                                + " on " + log.getDatabaseName()
                                + (tbName.empty() ? "" : "." + tbName));

            switch (opType) {
                case DbLogOperationType::Insert: {
                    // 撤销 INSERT：根据 afterData 中的主键删除刚插入的行
                    if (!tbName.empty() && !log.getAfterData().empty()) {
                        try {
                            auto data = nlohmann::json::parse(log.getAfterData());
                            if (data.contains("__primary_key__")) {
                                std::string pk = data["__primary_key__"].get<std::string>();
                                undoResult = databaseManager->deleteRowByPrimaryKey(databaseName, tbName, pk);
                            } else if (data.contains("values") && data["values"].is_array()
                                       && !data["values"].empty()) {
                                std::string pk = data["values"][0].get<std::string>();
                                undoResult = databaseManager->deleteRowByPrimaryKey(databaseName, tbName, pk);
                            }
                        } catch (...) {
                            LogWriter::warning("dbLog", "DbLogManager", "dbRecover",
                                               "Failed to undo INSERT: bad afterData format");
                        }
                    }
                    break;
                }
                case DbLogOperationType::Delete: {
                    // 撤销 DELETE：根据 beforeData 重新插入被删除的行
                    if (!tbName.empty() && !log.getBeforeData().empty()) {
                        try {
                            auto data = nlohmann::json::parse(log.getBeforeData());
                            if (data.contains("values") && data["values"].is_array()) {
                                std::vector<std::string> rowValues;
                                for (const auto &v : data["values"]) {
                                    rowValues.push_back(v.get<std::string>());
                                }
                                undoResult = databaseManager->insertRow(databaseName, tbName, rowValues);
                            }
                        } catch (...) {
                            LogWriter::warning("dbLog", "DbLogManager", "dbRecover",
                                               "Failed to undo DELETE: bad beforeData format");
                        }
                    }
                    break;
                }
                case DbLogOperationType::Update: {
                    // 撤销 UPDATE：根据 beforeData 恢复旧值
                    if (!tbName.empty() && !log.getBeforeData().empty()) {
                        try {
                            auto data = nlohmann::json::parse(log.getBeforeData());
                            if (data.contains("primary_key") && data.contains("old_values")
                                && data["old_values"].is_array()) {
                                std::string pk = data["primary_key"].get<std::string>();
                                std::vector<std::string> oldValues;
                                for (const auto &v : data["old_values"]) {
                                    oldValues.push_back(v.get<std::string>());
                                }
                                undoResult = databaseManager->updateRowByPrimaryKey(databaseName, tbName, pk, oldValues);
                            }
                        } catch (...) {
                            LogWriter::warning("dbLog", "DbLogManager", "dbRecover",
                                               "Failed to undo UPDATE: bad beforeData format");
                        }
                    }
                    break;
                }
                case DbLogOperationType::CreateTable: {
                    // 撤销 CREATE TABLE → DROP TABLE
                    if (!tbName.empty()) {
                        undoResult = databaseManager->dropTable(databaseName, tbName);
                    }
                    break;
                }
                case DbLogOperationType::DropTable: {
                    // 撤销 DROP TABLE：根据完整快照重建表和数据
                    if (!tbName.empty() && !log.getBeforeData().empty()) {
                        try {
                            undoResult = restoreTableSnapshot(
                                databaseManager,
                                databaseName,
                                nlohmann::json::parse(log.getBeforeData()));
                        } catch (...) {
                            LogWriter::warning("dbLog", "DbLogManager", "dbRecover",
                                               "Failed to undo DROP TABLE: bad beforeData format");
                        }
                    }
                    break;
                }
                case DbLogOperationType::CreateDatabase: {
                    undoResult = systemCatalogManager != nullptr
                                 && systemCatalogManager->dropDatabase(databaseName);
                    break;
                }
                case DbLogOperationType::DropDatabase: {
                    try {
                        undoResult = restoreDatabaseSnapshot(systemCatalogManager,
                                                             databaseManager,
                                                             databaseName,
                                                             nlohmann::json::parse(log.getBeforeData()));
                    } catch (...) {
                        LogWriter::warning("dbLog", "DbLogManager", "dbRecover",
                                           "Failed to undo DROP DATABASE: bad beforeData format");
                    }
                    break;
                }
                case DbLogOperationType::AlterTable: {
                    if (!tbName.empty() && !log.getBeforeData().empty()) {
                        try {
                            undoResult = restoreTableSnapshot(
                                databaseManager,
                                databaseName,
                                nlohmann::json::parse(log.getBeforeData()));
                        } catch (...) {
                            LogWriter::warning("dbLog", "DbLogManager", "dbRecover",
                                               "Failed to undo ALTER TABLE: bad beforeData format");
                        }
                    }
                    break;
                }
            }

            if (!undoResult) {
                recoverySucceeded = false;
                LogWriter::error("dbLog", "DbLogManager", "dbRecover",
                                 "Undo failed for operation [" + std::to_string(log.getOperationId())
                                     + "] " + LogBlock::operationTypeToString(opType));
                break;
            }
            ++undoneCount;
        } catch (const std::exception &e) {
            recoverySucceeded = false;
            LogWriter::error("dbLog", "DbLogManager", "dbRecover",
                             std::string("Exception during undo of operation ")
                                 + std::to_string(log.getOperationId()) + ": " + e.what());
            break;
        }
    }

    if (!recoverySucceeded) {
        return false;
    }

    if (systemCatalogManager != nullptr && systemCatalogManager->checkDbExists(databaseName)) {
        systemCatalogManager->addDatabaseVersion(databaseName);
    }

    LogWriter::info("dbLog", "DbLogManager", "dbRecover",
                    "Recovery completed. " + std::to_string(undoneCount)
                        + " operations undone for database: " + databaseName);

    return true;
}

std::vector<LogBlock> DbLogManager::getLogsForDatabase(const std::string &databaseName)
{
    std::map<std::int64_t, LogBlock> mergedLogs;
    const std::vector<std::filesystem::path> candidatePaths = {
        std::filesystem::path(getLogFilePath(databaseName)),
        getLegacyLogFilePath(databaseName)};

    for (const auto &logPath : candidatePaths) {
        if (logPath.empty() || !std::filesystem::exists(logPath)) {
            continue;
        }

        std::ifstream inFile(logPath);
        std::string line;
        while (std::getline(inFile, line)) {
            if (line.empty()) {
                continue;
            }
            LogBlock block;
            if (LogBlock::fromJsonString(line, block) && block.getDatabaseName() == databaseName) {
                mergedLogs[block.getOperationId()] = block;
            }
        }
    }

    std::vector<LogBlock> result;
    result.reserve(mergedLogs.size());
    for (const auto &entry : mergedLogs) {
        result.push_back(entry.second);
    }

    // 按时间戳升序排序，时间相同时按操作ID升序
    std::sort(result.begin(), result.end(),
              [](const LogBlock &a, const LogBlock &b) {
                  if (!dateTimeLessOrEqual(a.getTimestamp(), b.getTimestamp())
                      && dateTimeLessOrEqual(b.getTimestamp(), a.getTimestamp())) {
                      return false;
                  }
                  if (dateTimeLessOrEqual(a.getTimestamp(), b.getTimestamp())
                      && !dateTimeLessOrEqual(b.getTimestamp(), a.getTimestamp())) {
                      return true;
                  }
                  return a.getOperationId() < b.getOperationId();
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

    return (getDbLogRootPath() / (databaseName + ".log")).string();
}

DateTime DbLogManager::buildCurrentDateTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);

    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    DateTime dt;
    dt.setYear(static_cast<std::uint16_t>(localTime.tm_year + 1900));
    dt.setMonth(static_cast<std::uint16_t>(localTime.tm_mon + 1));
    dt.setDayOfWeek(static_cast<std::uint16_t>(localTime.tm_wday));
    dt.setDay(static_cast<std::uint16_t>(localTime.tm_mday));
    dt.setHour(static_cast<std::uint16_t>(localTime.tm_hour));
    dt.setMinute(static_cast<std::uint16_t>(localTime.tm_min));
    dt.setSecond(static_cast<std::uint16_t>(localTime.tm_sec));
    dt.setMilliseconds(static_cast<std::uint16_t>(milliseconds.count()));
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
