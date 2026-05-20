#include "InsertStmt.h"

InsertStmt::InsertStmt()
    : SQLStatement(ExecutionStatementType::Insert)
{
}

const std::string &InsertStmt::getTableName() const
{
    return tableName;
}

void InsertStmt::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}

const std::vector<std::string> &InsertStmt::getColumnNames() const
{
    return columnNames;
}

void InsertStmt::setColumnNames(const std::vector<std::string> &columnNames)
{
    this->columnNames = columnNames;
}

const std::vector<std::string> &InsertStmt::getValues() const
{
    return values;
}

void InsertStmt::setValues(const std::vector<std::string> &values)
{
    this->values = values;
}

const std::vector<std::vector<std::string>> &InsertStmt::getMultiValues() const
{
    return multiValues;
}

void InsertStmt::setMultiValues(const std::vector<std::vector<std::string>> &multiValues)
{
    this->multiValues = multiValues;
}

void InsertStmt::addValuesRow(const std::vector<std::string> &valuesRow)
{
    multiValues.push_back(valuesRow);
}
