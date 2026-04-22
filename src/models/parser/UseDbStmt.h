#pragma once

#include <string>

#include "SQLStatement.h"

/**
 * @class UseDbStmt
 * @brief 切换当前数据库语句数据类
 * @details 封装 USE DATABASE 语句解析后的目标数据库名称信息。
 * @author NAPH130
 */
class UseDbStmt : public SQLStatement
{
public:
    UseDbStmt();

    const std::string &getDbName() const;
    void setDbName(const std::string &dbName);

private:
    std::string dbName;
};
