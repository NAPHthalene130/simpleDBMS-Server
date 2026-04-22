#include "UpdateStmt.h"

UpdateStmt::UpdateStmt()
    : SQLStatement(ExecutionStatementType::Update)
{
}

const std::string &UpdateStmt::getTableName() const
{
    return tableName;
}

void UpdateStmt::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}

const std::vector<std::string> &UpdateStmt::getColumnNames() const
{
    return columnNames;
}

void UpdateStmt::setColumnNames(const std::vector<std::string> &columnNames)
{
    this->columnNames = columnNames;
}

const std::vector<std::string> &UpdateStmt::getValues() const
{
    return values;
}

void UpdateStmt::setValues(const std::vector<std::string> &values)
{
    this->values = values;
}

const std::shared_ptr<ConditionNode> &UpdateStmt::getWhereCondition() const
{
    return whereCondition;
}

void UpdateStmt::setWhereCondition(const std::shared_ptr<ConditionNode> &whereCondition)
{
    this->whereCondition = whereCondition;
}
