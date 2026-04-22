#include "SystemCatalogManager.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>

namespace
{
constexpr const char *DATABASE_FILE_PATH = "src/storage/data/database.db";

std::string fixedArrayToString(const std::array<char, 128> &value)
{
    const auto endIterator = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIterator);
}

bool isDatabaseBlockEmpty(const DatabaseBlock &databaseBlock)
{
    return fixedArrayToString(databaseBlock.getName()).empty();
}
}

SystemCatalogManager::SystemCatalogManager(Core *core)
    : core(core)
{
}

bool SystemCatalogManager::createDatabase(DatabaseBlock dbInfo)
{
    (void)dbInfo;
    return false;
}

bool SystemCatalogManager::dropDatabase(std::string dbName)
{
    (void)dbName;
    return false;
}

std::vector<DatabaseBlock> SystemCatalogManager::getAllDatabases()
{
    std::vector<DatabaseBlock> databaseBlocks;
    std::ifstream inputStream(DATABASE_FILE_PATH, std::ios::binary);
    if (!inputStream.is_open()) {
        return databaseBlocks;
    }

    while (inputStream.good()) {
        DatabaseBlock databaseBlock;
        inputStream.read(reinterpret_cast<char *>(&databaseBlock), sizeof(DatabaseBlock));
        if (inputStream.gcount() != static_cast<std::streamsize>(sizeof(DatabaseBlock))) {
            break;
        }

        if (!isDatabaseBlockEmpty(databaseBlock)) {
            databaseBlocks.push_back(databaseBlock);
        }
    }

    return databaseBlocks;
}

bool SystemCatalogManager::checkDbExists(std::string dbName)
{
    if (dbName.empty()) {
        return false;
    }

    const std::vector<DatabaseBlock> databaseBlocks = getAllDatabases();
    for (const DatabaseBlock &databaseBlock : databaseBlocks) {
        if (fixedArrayToString(databaseBlock.getName()) == dbName) {
            return true;
        }
    }

    return false;
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
