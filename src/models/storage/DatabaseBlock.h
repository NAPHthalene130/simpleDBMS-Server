#pragma once

#include <array>

#include "DateTime.h"

/**
 * @class DatabaseBlock
 * @brief 数据库描述块类
 * @details 封装单个数据库的元信息，包括数据库名称、数据库类型、描述文件路径和创建时间。
 * 该类用于数据库目录管理与数据库对象加载时的基础信息交换。
 * @author NAPH130
 */
class DatabaseBlock
{
public:
    DatabaseBlock();

    const std::array<char, 128> &getName() const;
    void setName(const std::array<char, 128> &name);

    bool getType() const;
    void setType(bool type);

    const std::array<char, 256> &getFileName() const;
    void setFileName(const std::array<char, 256> &fileName);

    const DateTime &getCreateTime() const;
    void setCreateTime(const DateTime &createTime);

private:
    std::array<char, 128> name;     ///< 数据库名称
    bool type;                      ///< 数据库类型标识（系统库/用户库）
    std::array<char, 256> fileName; ///< 数据库描述文件路径
    DateTime createTime;            ///< 数据库创建时间
};
