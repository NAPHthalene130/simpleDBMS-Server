#pragma once

class Core;
class CreateDbExecutor;
class CreateTableExecutor;
class DatabaseManager;
class DeleteExecutor;
class DropExecutor;
class ExecutorEngine;
class InsertExecutor;
class SelectExecutor;
class ShowExecutor;
class SystemCatalogManager;
class TableDefManager;
class UpdateExecutor;
class UseDbExecutor;
class UseExecutor;
class AlterTableExecutor;
class DclExecutor;

/**
 * @class ExecutorManager
 * @brief 执行模块总管理器
 * @details 统一维护执行层各组件实例，并负责构建执行器注册关系。
 * @author NAPH130
 */
class ExecutorManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit ExecutorManager(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~ExecutorManager();

    ExecutorEngine *getExecutorEngine() const;
    CreateDbExecutor *getCreateDbExecutor() const;
    CreateTableExecutor *getCreateTableExecutor() const;
    InsertExecutor *getInsertExecutor() const;
    SelectExecutor *getSelectExecutor() const;
    UseDbExecutor *getUseDbExecutor() const;
    UseExecutor *getUseExecutor() const;
    ShowExecutor *getShowExecutor() const;
    DropExecutor *getDropExecutor() const;
    DeleteExecutor *getDeleteExecutor() const;
    UpdateExecutor *getUpdateExecutor() const;

    AlterTableExecutor *getAlterTableExecutor() const;
    DclExecutor *getDclExecutor() const;

    SystemCatalogManager *getSystemCatalogManager() const;
    DatabaseManager *getDatabaseManager() const;
    TableDefManager *getTableDefManager() const;

private:
    Core *core;

    ExecutorEngine *executorEngine;
    CreateDbExecutor *createDbExecutor;
    CreateTableExecutor *createTableExecutor;
    InsertExecutor *insertExecutor;
    SelectExecutor *selectExecutor;
    UseDbExecutor *useDbExecutor;
    UseExecutor *useExecutor;
    ShowExecutor *showExecutor;
    DropExecutor *dropExecutor;
    DeleteExecutor *deleteExecutor;
    UpdateExecutor *updateExecutor;
    AlterTableExecutor *alterTableExecutor;
    DclExecutor *dclExecutor;
};
