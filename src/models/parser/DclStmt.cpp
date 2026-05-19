#include "DclStmt.h"

DclStmt::DclStmt()
    : SQLStatement(ExecutionStatementType::Dcl), operationType(DclOperationType::Grant)
{
}

DclOperationType DclStmt::getOperationType() const
{
    return operationType;
}

void DclStmt::setOperationType(DclOperationType operationType)
{
    this->operationType = operationType;
}

const std::string &DclStmt::getUserName() const
{
    return userName;
}

void DclStmt::setUserName(const std::string &userName)
{
    this->userName = userName;
}

const std::string &DclStmt::getPassword() const
{
    return password;
}

void DclStmt::setPassword(const std::string &password)
{
    this->password = password;
}
