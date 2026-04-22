#include "UseDbStmt.h"

UseDbStmt::UseDbStmt()
    : SQLStatement(ExecutionStatementType::UseDatabase)
{
}

const std::string &UseDbStmt::getDbName() const
{
    return dbName;
}

void UseDbStmt::setDbName(const std::string &dbName)
{
    this->dbName = dbName;
}
