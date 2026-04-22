#include "SystemCatalogManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

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
            return false;
        }

        const auto dbRootPath = std::filesystem::path(kDbRootPath);
        const auto dbFolderPath = dbRootPath / dbName;
        const auto dbFilePath = dbRootPath / (dbName + ".db");
        const auto dbDescFilePath = dbFolderPath / (dbName + ".tb");
        const auto dbLogFilePath = dbFolderPath / (dbName + ".log");

        if (std::filesystem::exists(dbFolderPath) || std::filesystem::exists(dbFilePath)) {
            return false;
        }

        std::filesystem::create_directories(dbFolderPath);

        std::ofstream dbFile(dbFilePath, std::ios::app);
        if (!dbFile.good()) {
            return false;
        }

        std::ofstream dbDescFile(dbDescFilePath, std::ios::app);
        if (!dbDescFile.good()) {
            return false;
        }

        std::ofstream dbLogFile(dbLogFilePath, std::ios::app);
        if (!dbLogFile.good()) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool SystemCatalogManager::dropDatabase(std::string dbName)
{
    if (dbName.empty()) {
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
        return removedFolder || removedDbFile;
    } catch (...) {
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
    return blocks;
}

bool SystemCatalogManager::checkDbExists(std::string dbName)
{
    if (dbName.empty()) {
        return false;
    }

    const auto dbRootPath = std::filesystem::path(kDbRootPath);
    const auto dbFolderPath = dbRootPath / dbName;
    const auto dbFilePath = dbRootPath / (dbName + ".db");
    return std::filesystem::exists(dbFolderPath) && std::filesystem::is_directory(dbFolderPath)
           && std::filesystem::exists(dbFilePath) && std::filesystem::is_regular_file(dbFilePath);
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
