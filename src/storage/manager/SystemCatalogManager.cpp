#include "SystemCatalogManager.h"

// 当前仅预留系统目录管理器实现入口。

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
    return {};
}

bool SystemCatalogManager::checkDbExists(std::string dbName)
{
    (void)dbName;
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
