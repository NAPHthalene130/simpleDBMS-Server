#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ConditionNode.h"
#include "SQLStatement.h"

/**
 * @class SelectStmt
 * @brief 查询语句数据类
 * @details 封装 SELECT 语句解析后的目标表、目标字段集合与 WHERE 条件树。
 * @author NAPH130
 */
class SelectStmt : public SQLStatement
{
public:
    SelectStmt();

    const std::string &getTableName() const;
    void setTableName(const std::string &tableName);

    const std::vector<std::string> &getTargetFields() const;
    void setTargetFields(const std::vector<std::string> &targetFields);

    bool getSelectAllFields() const;
    void setSelectAllFields(bool selectAllFields);

    const std::shared_ptr<ConditionNode> &getWhereCondition() const;
    void setWhereCondition(const std::shared_ptr<ConditionNode> &whereCondition);

private:
    std::string tableName;
    std::vector<std::string> targetFields;
    bool selectAllFields;
    std::shared_ptr<ConditionNode> whereCondition;
};
