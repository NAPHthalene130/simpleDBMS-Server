#include "SystemCatalogManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "log/LogWriter.h"

namespace {

constexpr const char *kDbRootPath = "data";

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
    block.setFileName(stringToArray<256>((std::filesystem::path(kDbRootPath) / (dbName + ".db")).string()));
    return block;
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

        const auto dbRootPath = std::filesystem::path(kDbRootPath);
        const auto dbFolderPath = dbRootPath / dbName;
        const auto dbFilePath = dbRootPath / (dbName + ".db");
        const auto dbDescFilePath = dbFolderPath / (dbName + ".tb");
        const auto dbLogFilePath = dbFolderPath / (dbName + ".log");

        if (std::filesystem::exists(dbFolderPath) || std::filesystem::exists(dbFilePath)) {
            LogWriter::warning("storage",
                               "SystemCatalogManager",
                               "createDatabase",
                               std::string("Database already exists: ") + dbName);
            return false;
        }

        std::filesystem::create_directories(dbFolderPath);

        std::ofstream dbFile(dbFilePath, std::ios::app);
        if (!dbFile.good()) {
            LogWriter::error("storage",
                             "SystemCatalogManager",
                             "createDatabase",
                             std::string("Failed to create database file for ") + dbName);
            return false;
        }

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
        const auto dbRootPath = std::filesystem::path(kDbRootPath);
        const auto dbFolderPath = dbRootPath / dbName;
        const auto dbFilePath = dbRootPath / (dbName + ".db");

        const bool removedFolder = std::filesystem::exists(dbFolderPath)
                                   && std::filesystem::remove_all(dbFolderPath) > 0;
        const bool removedDbFile = std::filesystem::exists(dbFilePath)
                                   && std::filesystem::remove(dbFilePath);
        LogWriter::info("storage",
                        "SystemCatalogManager",
                        "dropDatabase",
                        std::string("Drop database result for ") + dbName + ": "
                            + ((removedFolder || removedDbFile) ? "success" : "not found"));
        return removedFolder || removedDbFile;
    } catch (...) {
        LogWriter::error("storage", "SystemCatalogManager", "dropDatabase", "Unknown exception while dropping database.");
        return false;
    }
}

std::vector<DatabaseBlock> SystemCatalogManager::getAllDatabases()
{
    std::vector<DatabaseBlock> blocks;
    const auto dbRootPath = std::filesystem::path(kDbRootPath);

    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        return blocks;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() != ".db") {
            continue;
        }
        blocks.push_back(buildDatabaseBlock(entry.path().stem().string()));
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

    const auto dbRootPath = std::filesystem::path(kDbRootPath);
    const auto dbFolderPath = dbRootPath / dbName;
    const auto dbFilePath = dbRootPath / (dbName + ".db");
    const bool exists = std::filesystem::exists(dbFolderPath) && std::filesystem::is_directory(dbFolderPath)
                        && std::filesystem::exists(dbFilePath) && std::filesystem::is_regular_file(dbFilePath);
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
