/**
 * @file ResetSystemUserApp.cpp
 * @brief 系统用户强制重置程序
 * @details
 * 强制重置 storage/data 下 system 库的 user 表。
 * 若 system 库不存在则创建，user 表不存在则创建，已存在则先删除再重建。
 * 表结构：id VARCHAR(256) 和 password VARCHAR(1024)，默认数据 ('root', '123456')。
 * @author NAPH130
 */

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>

#include "Core.h"
#include "log/LogWriter.h"
#include "models/storage/DatabaseBlock.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/object/StorageCommon.h"

namespace {

/**
 * @brief 将字符串拷贝到固定长度数组
 * @author NAPH130
 * @param value 源字符串
 * @return 固定长度数组
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
 * @return 当前时间
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
 * @brief 构建 DatabaseBlock 对象
 * @author NAPH130
 * @param dbName 数据库名称
 * @return DatabaseBlock 对象
 */
DatabaseBlock buildDatabaseBlock(const std::string &dbName)
{
    DatabaseBlock databaseBlock;
    databaseBlock.setName(stringToArray<128>(dbName));
    databaseBlock.setType(true); // 系统库
    databaseBlock.setFileName(stringToArray<256>(
        (SystemCatalogManager::getDataRootPath() / dbName).string()));
    databaseBlock.setCreateTime(buildCurrentDateTime());
    return databaseBlock;
}

/**
 * @brief 检查指定库下是否存在指定表
 * @author NAPH130
 * @param databaseManager 数据库管理器
 * @param dbName 数据库名称
 * @param tableName 表名称
 * @return 表是否存在
 */
bool tableExists(DatabaseManager *databaseManager,
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

} // namespace

/**
 * @brief 系统用户强制重置入口
 * @details
 * 1. 初始化服务端核心模块。
 * 2. 检查 system 库是否存在，不存在则创建。
 * 3. 检查 user 表是否存在，存在则先删除再重建。
 * 4. 创建 user 表（id VARCHAR(256), password VARCHAR(1024)）。
 * 5. 插入默认账户 root/123456。
 * @author NAPH130
 */
int main()
{
    try {
        LogWriter::info("app", "ResetSystemUserApp", "main",
                        "ResetSystemUserApp is starting.");

        Core core;

        StorageManager *storageManager = core.getStorageManager();
        if (storageManager == nullptr) {
            LogWriter::fatal("app", "ResetSystemUserApp", "main",
                             "Storage manager is not initialized.");
            return 1;
        }

        SystemCatalogManager *systemCatalogManager =
            storageManager->getSystemCatalogManager();
        DatabaseManager *databaseManager = storageManager->getDatabaseManager();

        if (systemCatalogManager == nullptr || databaseManager == nullptr) {
            LogWriter::fatal("app", "ResetSystemUserApp", "main",
                             "Storage sub-managers are not initialized.");
            return 1;
        }

        const std::string systemDbName = "system";
        const std::string userTableName = "user";

        // 确保 system 库存在
        // 作者：NAPH130
        if (!systemCatalogManager->checkDbExists(systemDbName)) {
            const DatabaseBlock dbBlock = buildDatabaseBlock(systemDbName);
            if (!systemCatalogManager->createDatabase(dbBlock)) {
                LogWriter::fatal("app", "ResetSystemUserApp", "main",
                                 "Failed to create system database.");
                return 1;
            }
            LogWriter::info("app", "ResetSystemUserApp", "main",
                            "System database created.");
        } else {
            LogWriter::info("app", "ResetSystemUserApp", "main",
                            "System database already exists.");
        }

        // 若 user 表已存在则删除（强制重置）
        // 作者：NAPH130
        if (tableExists(databaseManager, systemDbName, userTableName)) {
            LogWriter::info("app", "ResetSystemUserApp", "main",
                            "User table exists, dropping before reset.");
            if (!databaseManager->dropTable(userTableName)) {
                LogWriter::fatal("app", "ResetSystemUserApp", "main",
                                 "Failed to drop existing user table.");
                return 1;
            }
            LogWriter::info("app", "ResetSystemUserApp", "main",
                            "Existing user table dropped.");
        }

        // 创建 user 表
        // 作者：NAPH130
        {
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
                LogWriter::fatal("app", "ResetSystemUserApp", "main",
                                 "Failed to create user table.");
                return 1;
            }
            LogWriter::info("app", "ResetSystemUserApp", "main",
                            "User table created.");
        }

        // 插入默认数据项
        // 作者：NAPH130
        {
            const std::vector<std::string> defaultValues = {"root", "123456"};
            if (!databaseManager->insertRow(systemDbName, userTableName, defaultValues)) {
                LogWriter::error("app", "ResetSystemUserApp", "main",
                                 "Failed to insert default user row.");
            } else {
                LogWriter::info("app", "ResetSystemUserApp", "main",
                                "Default user (root/123456) inserted.");
            }
        }

        std::cout << "ResetSystemUserApp completed." << std::endl;
        LogWriter::info("app", "ResetSystemUserApp", "main",
                        "ResetSystemUserApp completed successfully.");
    } catch (const std::exception &exception) {
        LogWriter::fatal("app", "ResetSystemUserApp", "main",
                         std::string("Unhandled exception: ") + exception.what());
        return 1;
    } catch (...) {
        LogWriter::fatal("app", "ResetSystemUserApp", "main",
                         "Unhandled unknown exception.");
        return 1;
    }

    return 0;
}
