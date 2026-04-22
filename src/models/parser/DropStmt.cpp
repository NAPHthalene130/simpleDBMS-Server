#include "DropStmt.h"

DropStmt::DropStmt()
    : SQLStatement(ExecutionStatementType::Drop), targetType(DropTargetType::Database)
{
}

DropTargetType DropStmt::getTargetType() const
{
    return targetType;
}

void DropStmt::setTargetType(DropTargetType targetType)
{
    this->targetType = targetType;
}

const std::string &DropStmt::getTargetName() const
{
    return targetName;
}

void DropStmt::setTargetName(const std::string &targetName)
{
    this->targetName = targetName;
}
