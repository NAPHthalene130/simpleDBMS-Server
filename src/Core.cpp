#include "Core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>

#include "binder/BinderManager.h"
#include "core/SqlPipeline.h"
#include "dbLog/DbLogManager.h"
#include "executor/ExecutorManager.h"
#include "log/LogWriter.h"
#include "models/storage/DatabaseBlock.h"
#include "network/NetworkManager.h"
#include "parser/ParserManager.h"
#include "plan/PlanManager.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/StorageCommon.h"
#include "tokenizer/Tokenizer.h"

namespace {

/**
 * @brief 将字符串拷贝到固定长度数组
 * @author NAPH130
 */
template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value)
{
    std::array<char, N> target{};
    const std::size_t copySize = std::min(value.size(), target.size() - 1);
    std::memcpy(target.data(), value.data(), copySize);
    return target;
}

/**
 * @brief 构建当前时间 DateTime 对象
 * @author NAPH130
 */
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

/**
 * @brief 检查指定库下是否存在指定表
 * @author NAPH130
 */
bool tableExistsInDb(DatabaseManager *databaseManager,
                     const std::string &dbName,
                     const std::string &tableName)
{
    if (databaseManager == nullptr) {
        return false;
    }
    const auto tables = databaseManager->getAllTablesForDb(dbName);
    return std::any_of(tables.begin(), tables.end(),
                       [&tableName](const TableBlock &tb) {
                           const auto endIt = std::find(tb.getName().begin(),
                                                        tb.getName().end(), '\0');
                           const std::string existingName(tb.getName().begin(), endIt);
                           return existingName == tableName;
                       });
}

/**
 * @brief 确保 system 库与 user 表存在
 * @details 服务端每次启动时自动检查并初始化系统用户表。
 *          若 system 库不存在则创建，若 user 表不存在则创建并插入默认数据。
 * @author NAPH130
 * @param storageManager 存储管理器指针
 */
void ensureSystemUserTable(StorageManager *storageManager)
{
    if (storageManager == nullptr) {
        LogWriter::error("core", "ensureSystemUserTable", "",
                         "Storage manager is null, skipping system user init.");
        return;
    }

    SystemCatalogManager *systemCatalogManager =
        storageManager->getSystemCatalogManager();
    DatabaseManager *databaseManager =
        storageManager->getDatabaseManager();

    if (systemCatalogManager == nullptr || databaseManager == nullptr) {
        LogWriter::error("core", "ensureSystemUserTable", "",
                         "Storage sub-managers are null, skipping system user init.");
        return;
    }

    const std::string systemDbName = "system";
    const std::string userTableName = "user";

    // 检查并创建 system 库
    // 作者：NAPH130
    if (!systemCatalogManager->checkDbExists(systemDbName)) {
        DatabaseBlock dbBlock;
        dbBlock.setName(stringToArray<128>(systemDbName));
        dbBlock.setType(true);
        dbBlock.setFileName(stringToArray<256>(
            (SystemCatalogManager::getDataRootPath() / systemDbName).string()));
        dbBlock.setCreateTime(buildCurrentDateTime());

        if (!systemCatalogManager->createDatabase(dbBlock)) {
            LogWriter::error("core", "ensureSystemUserTable", "",
                             "Failed to create system database.");
            return;
        }
        LogWriter::info("core", "ensureSystemUserTable", "",
                        "System database auto-created on startup.");
    }

    // 检查并创建 user 表
    // 作者：NAPH130
    if (!tableExistsInDb(databaseManager, systemDbName, userTableName)) {
        std::vector<std::string> columns = {"id", "password"};

        storage::ColumnMeta idMeta;
        idMeta.integrities = 1; // NOT_NULL
        idMeta.dataType = storage::DataType::VARCHAR;
        idMeta.varcharLen = 256;

        storage::ColumnMeta pwdMeta;
        pwdMeta.integrities = 1; // NOT_NULL
        pwdMeta.dataType = storage::DataType::VARCHAR;
        pwdMeta.varcharLen = 1024;

        std::vector<storage::ColumnMeta> columnMetas = {idMeta, pwdMeta};

        if (!databaseManager->createTable(systemDbName, userTableName,
                                          columns, columnMetas)) {
            LogWriter::error("core", "ensureSystemUserTable", "",
                             "Failed to create user table in system database.");
            return;
        }
        LogWriter::info("core", "ensureSystemUserTable", "",
                        "User table auto-created on startup.");

        // 插入默认数据项
        // 作者：NAPH130
        const std::vector<std::string> defaultValues = {"root", "123456"};
        if (!databaseManager->insertRow(systemDbName, userTableName, defaultValues)) {
            LogWriter::error("core", "ensureSystemUserTable", "",
                             "Failed to insert default user row on startup.");
        } else {
            LogWriter::info("core", "ensureSystemUserTable", "",
                            "Default user (root/123456) inserted on startup.");
        }
    }
}

} // namespace

Core::Core()
    : networkManager(new NetworkManager(this)),
      storageManager(new StorageManager(this)),
      executorManager(new ExecutorManager(this)),
      tokenizer(new Tokenizer(this)),
      parserManager(new ParserManager(this)),
      dbLogManager(new DbLogManager(this)),
      binderManager(new BinderManager(this)),
      planManager(new PlanManager(this)),
      sqlPipeline(new SqlPipeline(this))
{
    LogWriter::info("core", "Core", "Core", "Core modules initialized.");
}

Core::~Core()
{
    LogWriter::info("core", "Core", "~Core", "Core is shutting down.");
    stop();
    delete planManager;
    delete binderManager;
    delete sqlPipeline;
    delete parserManager;
    delete tokenizer;
    delete networkManager;
    delete storageManager;
    delete executorManager;
    delete dbLogManager;
    planManager = nullptr;
    binderManager = nullptr;
    sqlPipeline = nullptr;
    parserManager = nullptr;
    tokenizer = nullptr;
    networkManager = nullptr;
    storageManager = nullptr;
    executorManager = nullptr;
    dbLogManager = nullptr;
    LogWriter::info("core", "Core", "~Core", "Core modules released.");
}

void Core::start()
{
    // 服务端启动时自动检查并初始化 system 库与 user 表
    // 作者：NAPH130
    ensureSystemUserTable(storageManager);

    if (networkManager != nullptr) {
        LogWriter::info("core", "Core", "start", "Starting core services.");
        networkManager->start();
    }
}

void Core::stop()
{
    if (networkManager != nullptr) {
        LogWriter::info("core", "Core", "stop", "Stopping core services.");
        networkManager->stop();
    }
}

NetworkManager *Core::getNetworkManager()
{
    return networkManager;
}

ExecutorManager *Core::getExecutorManager()
{
    return executorManager;
}

StorageManager *Core::getStorageManager()
{
    return storageManager;
}

Tokenizer *Core::getTokenizer()
{
    return tokenizer;
}

ParserManager *Core::getParserManager()
{
    return parserManager;
}

BinderManager *Core::getBinderManager()
{
    return binderManager;
}

PlanManager *Core::getPlanManager()
{
    return planManager;
}

DbLogManager *Core::getDbLogManager()
{
    return dbLogManager;
}

SqlPipeline *Core::getSqlPipeline()
{
    return sqlPipeline;
}