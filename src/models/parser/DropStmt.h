#pragma once

#include <string>

#include "SQLStatement.h"

/**
 * @enum DropTargetType
 * @brief DROP 语句目标类型枚举
 * @details 用于区分 DROP DATABASE 与 DROP TABLE 两种目标对象。
 * @author YuzhSong
 */
enum class DropTargetType
{
    Database,
    Table
};

/**
 * @class DropStmt
 * @brief DROP 语句数据类
 * @details 封装 DROP 语句目标对象类型和对象名称，供执行器进行分发处理。
 * @author YuzhSong
 */
class DropStmt : public SQLStatement
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     */
    DropStmt();

    /**
     * @brief 获取 DROP 目标类型
     * @author YuzhSong
     * @return DROP 目标类型
     */
    DropTargetType getTargetType() const;

    /**
     * @brief 设置 DROP 目标类型
     * @author YuzhSong
     * @param targetType DROP 目标类型
     */
    void setTargetType(DropTargetType targetType);

    /**
     * @brief 获取目标对象名称
     * @author YuzhSong
     * @return 目标对象名称
     */
    const std::string &getTargetName() const;

    /**
     * @brief 设置目标对象名称
     * @author YuzhSong
     * @param targetName 目标对象名称
     */
    void setTargetName(const std::string &targetName);

private:
    DropTargetType targetType;
    std::string targetName;
};
