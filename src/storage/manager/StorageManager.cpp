#include "StorageManager.h"

#include "DatabaseManager.h"
#include "FileManager.h"
#include "SystemCatalogManager.h"
#include "TableDefManager.h"

StorageManager::StorageManager(Core *core)
    : core(core),
      fileManager(new FileManager(core)),
      systemCatalogManager(new SystemCatalogManager(core)),
      databaseManager(new DatabaseManager(core)),
      tableDefManager(new TableDefManager(core))
{
}

StorageManager::~StorageManager()
{
    delete tableDefManager;
    delete databaseManager;
    delete systemCatalogManager;
    delete fileManager;

    tableDefManager = nullptr;
    databaseManager = nullptr;
    systemCatalogManager = nullptr;
    fileManager = nullptr;
}

FileManager *StorageManager::getFileManager() const
{
    return fileManager;
}

SystemCatalogManager *StorageManager::getSystemCatalogManager() const
{
    return systemCatalogManager;
}

DatabaseManager *StorageManager::getDatabaseManager() const
{
    return databaseManager;
}

TableDefManager *StorageManager::getTableDefManager() const
{
    return tableDefManager;
}
