#pragma once

#include <array>
#include <vector>

#include "SQLStatement.h"
#include "models/storage/FieldBlock.h"

/**
 * @class CreateTableStmt
 * @brief 创建数据表语句数据类
 * @details 封装 CREATE TABLE 语句解析后的表名称与字段定义列表。
 * @author NAPH130
 */
class CreateTableStmt : public SQLStatement
{
public:
    CreateTableStmt();

    const std::array<char, 128> &getTableName() const;
    void setTableName(const std::array<char, 128> &tableName);

    const std::vector<FieldBlock> &getFields() const;
    void setFields(const std::vector<FieldBlock> &fields);

private:
    std::array<char, 128> tableName;
    std::vector<FieldBlock> fields;
};
