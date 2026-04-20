#include "DatabaseManager.h"

// 当前仅预留数据库管理器实现入口。

DatabaseManager::DatabaseManager(Core *core)
    : core(core)
{
}

bool DatabaseManager::createTable(TableBlock tbInfo)
{
    (void)tbInfo;
    return false;
}

bool DatabaseManager::dropTable(std::string tableName)
{
    (void)tableName;
    return false;
}

bool DatabaseManager::modifyTable(std::string tableName, TableBlock newTbInfo)
{
    (void)tableName;
    (void)newTbInfo;
    return false;
}

TableBlock DatabaseManager::getTableInfo(std::string tableName)
{
    (void)tableName;
    return {};
}

std::vector<TableBlock> DatabaseManager::getAllTables()
{
    return {};
}
