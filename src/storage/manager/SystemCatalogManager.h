#pragma once

#include <string>
#include <vector>

#include "storage/models/DatabaseBlock.h"

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
};
