#include "StorageManager.h"

#include "DatabaseManager.h"
#include "FileManager.h"
#include "SystemCatalogManager.h"
#include "TableDefManager.h"
#include "log/LogWriter.h"

StorageManager::StorageManager(Core *core)
    : core(core),
      fileManager(new FileManager(core)),
      systemCatalogManager(new SystemCatalogManager(core)),
      databaseManager(new DatabaseManager(core)),
      tableDefManager(new TableDefManager(core))
{
    LogWriter::info("storage", "StorageManager", "StorageManager", "Storage manager initialized.");
}

StorageManager::~StorageManager()
{
    LogWriter::info("storage", "StorageManager", "~StorageManager", "Storage manager is being released.");
    delete tableDefManager;
    delete databaseManager;
    delete systemCatalogManager;
    delete fileManager;

    tableDefManager = nullptr;
    databaseManager = nullptr;
    systemCatalogManager = nullptr;
    fileManager = nullptr;
    LogWriter::info("storage", "StorageManager", "~StorageManager", "Storage sub-managers released.");
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
