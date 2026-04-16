#include "CreateDbStmt.h"

CreateDbStmt::CreateDbStmt()
    : SQLStatement(StatementType::CreateDatabase)
{
}

const std::string &CreateDbStmt::getDbName() const
{
    return dbName;
}

void CreateDbStmt::setDbName(const std::string &dbName)
{
    this->dbName = dbName;
}
