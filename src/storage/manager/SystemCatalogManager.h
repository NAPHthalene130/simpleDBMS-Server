#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class Core;

using uInt64 = std::uint64_t;

#include "models/storage/DatabaseBlock.h"

/**
 * @class SystemCatalogManager
 * @brief 系统目录管理器
 * @details 负责管理数据库系统目录与元信息，当前阶段仅预留数据库相关接口。
 * @author NAPH130
 */
class SystemCatalogManager
{
public:
    /**
     * @brief 获取数据存储根目录的绝对路径
     * @author NAPH130
     * @return 数据根目录路径
     */
    static const std::filesystem::path &getDataRootPath();
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit SystemCatalogManager(Core *core);

    /**
     * @brief 创建数据库
     * @author NAPH130
     * @param dbInfo 数据库元信息
     * @return 是否创建成功
     * @note 当前仅预留接口，暂未实现
     */
    bool createDatabase(DatabaseBlock dbInfo);

    /**
     * @brief 删除数据库
     * @author NAPH130
     * @param dbName 数据库名称
     * @return 是否删除成功
     * @note 当前仅预留接口，暂未实现
     */
    bool dropDatabase(std::string dbName);

    /**
     * @brief 获取所有数据库信息
     * @author NAPH130
     * @return 数据库元信息列表
     * @note 当前仅预留接口，暂未实现
     */
    std::vector<DatabaseBlock> getAllDatabases();

    /**
     * @brief 检查数据库是否存在
     * @author NAPH130
     * @param dbName 数据库名称
     * @return 数据库是否存在
     * @note 当前仅预留接口，暂未实现
     */
    bool checkDbExists(std::string dbName);

    /**
     * @brief 获取数据库版本号
     * @author GPT-5.4
     * @param dbName 数据库名称
     * @return 对应数据库当前版本号，不存在时返回 0
     */
    uInt64 getDatabaseVersion(std::string dbName);

    /**
     * @brief 增加数据库版本号
     * @author GPT-5.4
     * @param dbName 数据库名称
     * @details 若数据库尚无版本记录，则初始化为 1；否则在原值基础上加 1
     */
    void addDatabaseVersion(std::string dbName);

private:
    Core *core;
};
