#include "CreateTableStmt.h"

CreateTableStmt::CreateTableStmt()
    : SQLStatement(StatementType::CreateTable)
{
}

const std::array<char, 128> &CreateTableStmt::getTableName() const
{
    return tableName;
}

void CreateTableStmt::setTableName(const std::array<char, 128> &tableName)
{
    this->tableName = tableName;
}

const std::vector<FieldBlock> &CreateTableStmt::getFields() const
{
    return fields;
}

void CreateTableStmt::setFields(const std::vector<FieldBlock> &fields)
{
    this->fields = fields;
}
