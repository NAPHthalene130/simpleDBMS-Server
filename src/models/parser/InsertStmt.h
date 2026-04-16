#pragma once

#include <string>
#include <vector>

#include "SQLStatement.h"

/**
 * @class InsertStmt
 * @brief 插入语句数据类
 * @details 封装 INSERT INTO 语句解析后的表名、字段名列表与对应值列表。
 * @author NAPH130
 */
class InsertStmt : public SQLStatement
{
public:
    InsertStmt();

    const std::string &getTableName() const;
    void setTableName(const std::string &tableName);

    const std::vector<std::string> &getColumnNames() const;
    void setColumnNames(const std::vector<std::string> &columnNames);

    const std::vector<std::string> &getValues() const;
    void setValues(const std::vector<std::string> &values);

private:
    std::string tableName;
    std::vector<std::string> columnNames;
    std::vector<std::string> values;
};
