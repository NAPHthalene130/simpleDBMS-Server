#include "SystemCatalogManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "log/LogWriter.h"

namespace {

/**
 * @brief 获取数据存储根目录的绝对路径
 * @author NAPH130
 * @return 数据根目录路径（基于源文件位置，不依赖进程工作目录）
 * @details 本文件位于 src/storage/manager/，数据目录期望在 src/storage/data/。
 *          通过 __FILE__ 向上两级获得 storage/ 再拼接 data/，确保无论从何处启动服务端都写入正确位置。
 */
const std::filesystem::path &getDataRootPath()
{
    static const std::filesystem::path dataRoot =
        (std::filesystem::path(__FILE__).parent_path().parent_path() / "data").lexically_normal();
    return dataRoot;
}

const std::filesystem::path &getDatabaseCatalogPath()
{
    static const std::filesystem::path catalogPath = getDataRootPath() / "database.db";
    return catalogPath;
}

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value)
{
    std::array<char, N> result{};
    const auto copyLen = std::min<std::size_t>(value.size(), N - 1);
    std::memcpy(result.data(), value.data(), copyLen);
    return result;
}

DatabaseBlock buildDatabaseBlock(const std::string &dbName)
{
    DatabaseBlock block;
    block.setName(stringToArray<128>(dbName));
    block.setType(false);
    block.setFileName(stringToArray<256>(getDatabaseCatalogPath().string()));
    return block;
}

std::vector<std::string> readDatabaseCatalog()
{
    std::vector<std::string> names;
    const auto &catalogPath = getDatabaseCatalogPath();
    if (!std::filesystem::exists(catalogPath)) {
        return names;
    }

    std::ifstream ifs(catalogPath);
    if (!ifs.good()) {
        return names;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        names.push_back(line);
    }
    return names;
}

bool writeDatabaseCatalog(const std::vector<std::string> &names)
{
    std::filesystem::create_directories(getDataRootPath());
    std::ofstream ofs(getDatabaseCatalogPath(), std::ios::trunc);
    if (!ofs.good()) {
        return false;
    }
    for (const auto &name : names) {
        ofs << name << '\n';
    }
    return true;
}

} // namespace

SystemCatalogManager::SystemCatalogManager(Core *core)
    : core(core)
{
}

bool SystemCatalogManager::createDatabase(DatabaseBlock dbInfo)
{
    try {
        const std::string dbName = arrayToString(dbInfo.getName());
        if (dbName.empty()) {
            LogWriter::warning("storage", "SystemCatalogManager", "createDatabase", "Rejected empty database name.");
            return false;
        }

        const auto &dbRootPath = getDataRootPath();
        const auto dbFolderPath = dbRootPath / dbName;
        const auto dbDescFilePath = dbFolderPath / (dbName + ".tb");
        const auto dbLogFilePath = dbFolderPath / (dbName + ".log");
        auto databaseNames = readDatabaseCatalog();

        if (std::filesystem::exists(dbFolderPath)
            || std::find(databaseNames.begin(), databaseNames.end(), dbName) != databaseNames.end()) {
            LogWriter::warning("storage",
                               "SystemCatalogManager",
                               "createDatabase",
                               std::string("Database already exists: ") + dbName);
            return false;
        }

        std::filesystem::create_directories(dbFolderPath);

        std::ofstream dbDescFile(dbDescFilePath, std::ios::app);
        if (!dbDescFile.good()) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to create database descriptor for ") + dbName);
            return false;
        }

        std::ofstream dbLogFile(dbLogFilePath, std::ios::app);
        if (!dbLogFile.good()) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to create database log file for ") + dbName);
            return false;
        }

        databaseNames.push_back(dbName);
        if (!writeDatabaseCatalog(databaseNames)) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to update database catalog for ") + dbName);
            return false;
        }
        LogWriter::info("storage",
                        "SystemCatalogManager",
                        "createDatabase",
                        std::string("Database created successfully: ") + dbName);
        return true;
    } catch (...) {
        LogWriter::error("storage", "SystemCatalogManager", "createDatabase", "Unknown exception while creating database.");
        return false;
    }
}

bool SystemCatalogManager::dropDatabase(std::string dbName)
{
    if (dbName.empty()) {
        LogWriter::warning("storage", "SystemCatalogManager", "dropDatabase", "Rejected empty database name.");
        return false;
    }

    try {
        const auto &dbRootPath = getDataRootPath();
        const auto dbFolderPath = dbRootPath / dbName;
        auto databaseNames = readDatabaseCatalog();
        const auto newEnd = std::remove(databaseNames.begin(), databaseNames.end(), dbName);
        const bool removedCatalog = newEnd != databaseNames.end();
        databaseNames.erase(newEnd, databaseNames.end());

        const bool removedFolder = std::filesystem::exists(dbFolderPath)
                                   && std::filesystem::remove_all(dbFolderPath) > 0;
        const bool catalogUpdated = removedCatalog ? writeDatabaseCatalog(databaseNames) : true;
        if (!catalogUpdated) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "dropDatabase",
                             std::string("Failed to update database catalog while dropping ") + dbName);
            return false;
        }
        LogWriter::info("storage",
                        "SystemCatalogManager",
                        "dropDatabase",
                        std::string("Drop database result for ") + dbName + ": "
                            + ((removedFolder || removedCatalog) ? "success" : "not found"));
        return removedFolder || removedCatalog;
    } catch (...) {
        LogWriter::error("storage", "SystemCatalogManager", "dropDatabase", "Unknown exception while dropping database.");
        return false;
    }
}

std::vector<DatabaseBlock> SystemCatalogManager::getAllDatabases()
{
    std::vector<DatabaseBlock> blocks;
    const auto &dbRootPath = getDataRootPath();

    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        return blocks;
    }

    for (const auto &dbName : readDatabaseCatalog()) {
        const auto dbFolderPath = dbRootPath / dbName;
        if (!std::filesystem::exists(dbFolderPath) || !std::filesystem::is_directory(dbFolderPath)) {
            continue;
        }
        blocks.push_back(buildDatabaseBlock(dbName));
    }
    LogWriter::debug("storage",
                     "SystemCatalogManager",
                     "getAllDatabases",
                     std::string("Enumerated database count=") + std::to_string(blocks.size()));
    return blocks;
}

bool SystemCatalogManager::checkDbExists(std::string dbName)
{
    if (dbName.empty()) {
        LogWriter::warning("storage", "SystemCatalogManager", "checkDbExists", "Rejected empty database name.");
        return false;
    }

    const auto &dbRootPath = getDataRootPath();
    const auto dbFolderPath = dbRootPath / dbName;
    const auto databaseNames = readDatabaseCatalog();
    const bool inCatalog = std::find(databaseNames.begin(), databaseNames.end(), dbName) != databaseNames.end();
    const bool exists = std::filesystem::exists(dbFolderPath) && std::filesystem::is_directory(dbFolderPath)
                        && inCatalog;
    LogWriter::debug("storage",
                      "SystemCatalogManager",
                      "checkDbExists",
                     std::string("Database existence check for ") + dbName + ": " + (exists ? "true" : "false"));
    return exists;
}

uInt64 SystemCatalogManager::getDatabaseVersion(std::string dbName)
{
    (void)dbName;
    return 0;
}

void SystemCatalogManager::addDatabaseVersion(std::string dbName)
{
    (void)dbName;
}
