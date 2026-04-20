#pragma once

class Core;
class DatabaseManager;
class FileManager;
class SystemCatalogManager;
class TableDefManager;

/**
 * @class StorageManager
 * @brief 存储模块总管理器
 * @details 统一维护存储层管理类实例，并作为 Core 的存储访问入口。
 * @author NAPH130
 */
class StorageManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit StorageManager(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~StorageManager();

    FileManager *getFileManager() const;
    SystemCatalogManager *getSystemCatalogManager() const;
    DatabaseManager *getDatabaseManager() const;
    TableDefManager *getTableDefManager() const;

private:
    Core *core;
    FileManager *fileManager;
    SystemCatalogManager *systemCatalogManager;
    DatabaseManager *databaseManager;
    TableDefManager *tableDefManager;
};
