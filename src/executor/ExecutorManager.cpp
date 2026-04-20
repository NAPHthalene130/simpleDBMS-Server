#include "ExecutorManager.h"

#include "Core.h"
#include "ExecutorEngine.h"
#include "statementExecutors/CreateDbExecutor.h"
#include "statementExecutors/CreateTableExecutor.h"
#include "statementExecutors/InsertExecutor.h"
#include "statementExecutors/SelectExecutor.h"
#include "storage/manager/StorageManager.h"

ExecutorManager::ExecutorManager(Core *core)
    : core(core),
      executorEngine(new ExecutorEngine(core)),
      createDbExecutor(new CreateDbExecutor(core, getSystemCatalogManager())),
      createTableExecutor(new CreateTableExecutor(core, getDatabaseManager(), getTableDefManager())),
      insertExecutor(new InsertExecutor(core, getDatabaseManager(), getTableDefManager())),
      selectExecutor(new SelectExecutor(core, getDatabaseManager(), getTableDefManager()))
{
    executorEngine->registerExecutor(createDbExecutor);
    executorEngine->registerExecutor(createTableExecutor);
    executorEngine->registerExecutor(insertExecutor);
    executorEngine->registerExecutor(selectExecutor);
}

ExecutorManager::~ExecutorManager()
{
    delete selectExecutor;
    delete insertExecutor;
    delete createTableExecutor;
    delete createDbExecutor;
    delete executorEngine;

    selectExecutor = nullptr;
    insertExecutor = nullptr;
    createTableExecutor = nullptr;
    createDbExecutor = nullptr;
    executorEngine = nullptr;
}

ExecutorEngine *ExecutorManager::getExecutorEngine() const
{
    return executorEngine;
}

CreateDbExecutor *ExecutorManager::getCreateDbExecutor() const
{
    return createDbExecutor;
}

CreateTableExecutor *ExecutorManager::getCreateTableExecutor() const
{
    return createTableExecutor;
}

InsertExecutor *ExecutorManager::getInsertExecutor() const
{
    return insertExecutor;
}

SelectExecutor *ExecutorManager::getSelectExecutor() const
{
    return selectExecutor;
}

SystemCatalogManager *ExecutorManager::getSystemCatalogManager() const
{
    return core != nullptr && core->getStorageManager() != nullptr
               ? core->getStorageManager()->getSystemCatalogManager()
               : nullptr;
}

DatabaseManager *ExecutorManager::getDatabaseManager() const
{
    return core != nullptr && core->getStorageManager() != nullptr
               ? core->getStorageManager()->getDatabaseManager()
               : nullptr;
}

TableDefManager *ExecutorManager::getTableDefManager() const
{
    return core != nullptr && core->getStorageManager() != nullptr
               ? core->getStorageManager()->getTableDefManager()
               : nullptr;
}
