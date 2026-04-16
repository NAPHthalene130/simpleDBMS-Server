#pragma once

#include <string>

#include "SQLStatement.h"

/**
 * @class CreateDbStmt
 * @brief 创建数据库语句数据类
 * @details 封装 CREATE DATABASE 语句解析后的数据库名称信息。
 * @author NAPH130
 */
class CreateDbStmt : public SQLStatement
{
public:
    CreateDbStmt();

    const std::string &getDbName() const;
    void setDbName(const std::string &dbName);

private:
    std::string dbName;
};
