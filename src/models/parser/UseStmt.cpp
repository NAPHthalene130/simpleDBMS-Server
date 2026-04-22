#include "UseStmt.h"

UseStmt::UseStmt()
    : SQLStatement(ExecutionStatementType::Use)
{
}

const std::string &UseStmt::getDbName() const
{
    return dbName;
}

void UseStmt::setDbName(const std::string &dbName)
{
    this->dbName = dbName;
}
