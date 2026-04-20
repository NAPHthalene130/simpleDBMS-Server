#include "SelectStmt.h"

SelectStmt::SelectStmt()
    : SQLStatement(ExecutionStatementType::Select), selectAllFields(false)
{
}

const std::string &SelectStmt::getTableName() const
{
    return tableName;
}

void SelectStmt::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}

const std::vector<std::string> &SelectStmt::getTargetFields() const
{
    return targetFields;
}

void SelectStmt::setTargetFields(const std::vector<std::string> &targetFields)
{
    this->targetFields = targetFields;
}

bool SelectStmt::getSelectAllFields() const
{
    return selectAllFields;
}

void SelectStmt::setSelectAllFields(bool selectAllFields)
{
    this->selectAllFields = selectAllFields;
}

const std::shared_ptr<ConditionNode> &SelectStmt::getWhereCondition() const
{
    return whereCondition;
}

void SelectStmt::setWhereCondition(const std::shared_ptr<ConditionNode> &whereCondition)
{
    this->whereCondition = whereCondition;
}
