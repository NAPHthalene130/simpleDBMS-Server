#pragma once

#include <string>

#include "SQLStatement.h"

/**
 * @enum ShowTargetType
 * @brief SHOW 语句目标类型枚举
 * @details 用于区分 SHOW DATABASES、SHOW TABLES 以及按名称查看的 SHOW DATABASE/SHOW TABLE。
 * @author YuzhSong
 */
enum class ShowTargetType
{
    Databases,
    Tables,
    Database,
    Table
};

/**
 * @class ShowStmt
 * @brief SHOW 语句数据类
 * @details 封装 SHOW 语句目标类型与可选目标名称，供执行器按子类型返回结果集。
 * @author YuzhSong
 */
class ShowStmt : public SQLStatement
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     */
    ShowStmt();

    /**
     * @brief 获取 SHOW 目标类型
     * @author YuzhSong
     * @return SHOW 目标类型
     */
    ShowTargetType getTargetType() const;

    /**
     * @brief 设置 SHOW 目标类型
     * @author YuzhSong
     * @param targetType SHOW 目标类型
     */
    void setTargetType(ShowTargetType targetType);

    /**
     * @brief 获取可选目标名称
     * @author YuzhSong
     * @return 目标名称
     */
    const std::string &getTargetName() const;

    /**
     * @brief 设置可选目标名称
     * @author YuzhSong
     * @param targetName 目标名称
     */
    void setTargetName(const std::string &targetName);

private:
    ShowTargetType targetType;
    std::string targetName;
};
