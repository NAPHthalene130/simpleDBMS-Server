#include "ExecutorManager.h"

#include "Core.h"
#include "ExecutorEngine.h"
#include "statementExecutors/CreateDbExecutor.h"
#include "statementExecutors/CreateTableExecutor.h"
#include "statementExecutors/DeleteExecutor.h"
#include "statementExecutors/DropExecutor.h"
#include "statementExecutors/InsertExecutor.h"
#include "statementExecutors/SelectExecutor.h"
#include "statementExecutors/UseDbExecutor.h"
#include "log/LogWriter.h"
#include "statementExecutors/ShowExecutor.h"
#include "statementExecutors/UpdateExecutor.h"
#include "statementExecutors/UseExecutor.h"
#include "storage/manager/StorageManager.h"

ExecutorManager::ExecutorManager(Core *core)
    : core(core),
      executorEngine(new ExecutorEngine(core)),
      createDbExecutor(new CreateDbExecutor(core, getSystemCatalogManager())),
      createTableExecutor(new CreateTableExecutor(core, getDatabaseManager(), getTableDefManager())),
      insertExecutor(new InsertExecutor(core, getDatabaseManager(), getTableDefManager())),
      selectExecutor(new SelectExecutor(core,
                                        getSystemCatalogManager(),
                                        getDatabaseManager(),
                                        getTableDefManager())),
      useDbExecutor(new UseDbExecutor(core, getSystemCatalogManager())),
      useExecutor(new UseExecutor(core, getSystemCatalogManager())),
      showExecutor(new ShowExecutor(core, getSystemCatalogManager(), getDatabaseManager())),
      dropExecutor(new DropExecutor(core, getSystemCatalogManager(), getDatabaseManager())),
      deleteExecutor(new DeleteExecutor(core, getDatabaseManager())),
      updateExecutor(new UpdateExecutor(core, getDatabaseManager()))
{
    LogWriter::info("executor", "ExecutorManager", "ExecutorManager", "Executor manager initialized.");
    executorEngine->registerExecutor(createDbExecutor);
    executorEngine->registerExecutor(createTableExecutor);
    executorEngine->registerExecutor(insertExecutor);
    executorEngine->registerExecutor(selectExecutor);
    executorEngine->registerExecutor(useDbExecutor);
    LogWriter::info("executor", "ExecutorManager", "ExecutorManager", "All default executors were registered.");
    executorEngine->registerExecutor(useExecutor);
    executorEngine->registerExecutor(showExecutor);
    executorEngine->registerExecutor(dropExecutor);
    executorEngine->registerExecutor(deleteExecutor);
    executorEngine->registerExecutor(updateExecutor);
}

ExecutorManager::~ExecutorManager()
{
    LogWriter::info("executor", "ExecutorManager", "~ExecutorManager", "Executor manager is being destroyed.");
    delete useDbExecutor;
    delete updateExecutor;
    delete deleteExecutor;
    delete dropExecutor;
    delete showExecutor;
    delete useExecutor;
    delete selectExecutor;
    delete insertExecutor;
    delete createTableExecutor;
    delete createDbExecutor;
    delete executorEngine;

    updateExecutor = nullptr;
    deleteExecutor = nullptr;
    dropExecutor = nullptr;
    showExecutor = nullptr;
    useExecutor = nullptr;
    selectExecutor = nullptr;
    insertExecutor = nullptr;
    createTableExecutor = nullptr;
    createDbExecutor = nullptr;
    useDbExecutor = nullptr;
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

UseDbExecutor *ExecutorManager::getUseDbExecutor() const
{
    return useDbExecutor;
}

UseExecutor *ExecutorManager::getUseExecutor() const
{
    return useExecutor;
}

ShowExecutor *ExecutorManager::getShowExecutor() const
{
    return showExecutor;
}

DropExecutor *ExecutorManager::getDropExecutor() const
{
    return dropExecutor;
}

DeleteExecutor *ExecutorManager::getDeleteExecutor() const
{
    return deleteExecutor;
}

UpdateExecutor *ExecutorManager::getUpdateExecutor() const
{
    return updateExecutor;
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
