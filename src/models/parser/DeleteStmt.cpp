#include "DeleteStmt.h"

DeleteStmt::DeleteStmt()
    : SQLStatement(ExecutionStatementType::Delete)
{
}

const std::string &DeleteStmt::getTableName() const
{
    return tableName;
}

void DeleteStmt::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}

const std::shared_ptr<ConditionNode> &DeleteStmt::getWhereCondition() const
{
    return whereCondition;
}

void DeleteStmt::setWhereCondition(const std::shared_ptr<ConditionNode> &whereCondition)
{
    this->whereCondition = whereCondition;
}
