#include "AlterTableStmt.h"

AlterTableStmt::AlterTableStmt()
    : SQLStatement(ExecutionStatementType::AlterTable), targetType(AlterTableTargetType::AddColumn)
{
}

const std::string &AlterTableStmt::getTableName() const
{
    return tableName;
}

void AlterTableStmt::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}

AlterTableTargetType AlterTableStmt::getTargetType() const
{
    return targetType;
}

void AlterTableStmt::setTargetType(AlterTableTargetType targetType)
{
    this->targetType = targetType;
}

const std::string &AlterTableStmt::getColumnName() const
{
    return columnName;
}

void AlterTableStmt::setColumnName(const std::string &columnName)
{
    this->columnName = columnName;
}

const std::string &AlterTableStmt::getNewColumnName() const
{
    return newColumnName;
}

void AlterTableStmt::setNewColumnName(const std::string &newColumnName)
{
    this->newColumnName = newColumnName;
}

const std::string &AlterTableStmt::getColumnType() const
{
    return columnType;
}

void AlterTableStmt::setColumnType(const std::string &columnType)
{
    this->columnType = columnType;
}

std::uint16_t AlterTableStmt::getVarcharLen() const
{
    return varcharLen;
}

void AlterTableStmt::setVarcharLen(std::uint16_t varcharLen)
{
    this->varcharLen = varcharLen;
}

const std::string &AlterTableStmt::getDefaultValue() const
{
    return defaultValue;
}

void AlterTableStmt::setDefaultValue(const std::string &defaultValue)
{
    this->defaultValue = defaultValue;
}
