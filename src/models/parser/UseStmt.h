#pragma once

#include <string>

#include "SQLStatement.h"

/**
 * @class UseStmt
 * @brief USE 语句数据类
 * @details 封装 USE 语句解析后的目标数据库名称，供执行器更新会话数据库上下文。
 * @author YuzhSong
 */
class UseStmt : public SQLStatement
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     */
    UseStmt();

    /**
     * @brief 获取目标数据库名称
     * @author YuzhSong
     * @return 目标数据库名称
     */
    const std::string &getDbName() const;

    /**
     * @brief 设置目标数据库名称
     * @author YuzhSong
     * @param dbName 目标数据库名称
     */
    void setDbName(const std::string &dbName);

private:
    std::string dbName;
};
