#pragma once

#include <string>
#include "storage/object/StorageCommon.h"

class Core;
class DatabaseManager;

/**
 * @class TableDefManager
 * @brief 表定义管理器，承载 DDL 语义校验和元数据变更
 * @author NAPH130
 */
class TableDefManager
{
public:
    explicit TableDefManager(Core *core);

    /**
     * @brief 校验列定义是否合法
     */
    static bool validateColumn(const std::string& name, storage::DataType type, std::uint16_t varcharLen);

    /**
     * @brief 校验重命名是否合法
     */
    static bool validateRename(const std::string& oldName, const std::string& newName);

private:
    Core *core;
};
