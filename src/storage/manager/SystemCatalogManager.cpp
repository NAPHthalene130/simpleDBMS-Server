#include "SystemCatalogManager.h"

// 当前仅预留系统目录管理器实现入口。

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
