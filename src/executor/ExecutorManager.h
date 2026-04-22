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
class UseDbExecutor;
class UpdateExecutor;
class UseExecutor;

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

    /**
     * @brief 获取 USE 语句执行器
     * @author YuzhSong
     * @return USE 语句执行器指针
     */
    UseExecutor *getUseExecutor() const;

    /**
     * @brief 获取 SHOW 语句执行器
     * @author YuzhSong
     * @return SHOW 语句执行器指针
     */
    ShowExecutor *getShowExecutor() const;

    /**
     * @brief 获取 DROP 语句执行器
     * @author YuzhSong
     * @return DROP 语句执行器指针
     */
    DropExecutor *getDropExecutor() const;

    /**
     * @brief 获取 DELETE 语句执行器
     * @author YuzhSong
     * @return DELETE 语句执行器指针
     */
    DeleteExecutor *getDeleteExecutor() const;

    /**
     * @brief 获取 UPDATE 语句执行器
     * @author YuzhSong
     * @return UPDATE 语句执行器指针
     */
    UpdateExecutor *getUpdateExecutor() const;

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
};
