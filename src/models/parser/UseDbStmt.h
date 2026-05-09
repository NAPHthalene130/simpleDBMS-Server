#pragma once

#include <string>

#include "SQLStatement.h"

/**
 * @class UseDbStmt
 * @brief 切换数据库语句数据类
 * @details 封装 USE <DATABASE> 语句解析后的目标数据库名称。
 * @author NAPH130
 */
class UseDbStmt : public SQLStatement
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     */
    UseDbStmt();

    /**
     * @brief 获取目标数据库名称
     * @author NAPH130
     * @return 目标数据库名称
     */
    const std::string &getDbName() const;

    /**
     * @brief 设置目标数据库名称
     * @author NAPH130
     * @param dbName 目标数据库名称
     */
    void setDbName(const std::string &dbName);

private:
    std::string dbName;
};
