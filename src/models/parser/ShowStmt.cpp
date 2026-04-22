#include "ShowStmt.h"

ShowStmt::ShowStmt()
    : SQLStatement(ExecutionStatementType::Show), targetType(ShowTargetType::Databases)
{
}

ShowTargetType ShowStmt::getTargetType() const
{
    return targetType;
}

void ShowStmt::setTargetType(ShowTargetType targetType)
{
    this->targetType = targetType;
}

const std::string &ShowStmt::getTargetName() const
{
    return targetName;
}

void ShowStmt::setTargetName(const std::string &targetName)
{
    this->targetName = targetName;
}
