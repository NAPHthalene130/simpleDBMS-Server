#include "UnionStmt.h"

#include "SelectStmt.h"

UnionStmt::UnionStmt(bool unionAll)
    : SQLStatement(ExecutionStatementType::UnionSelect)
    , unionAll(unionAll)
{
}

const std::shared_ptr<SQLStatement> &UnionStmt::getLeftStmt() const
{
    return leftStmt;
}

void UnionStmt::setLeftStmt(const std::shared_ptr<SQLStatement> &leftStmt)
{
    this->leftStmt = leftStmt;
}

const std::shared_ptr<SQLStatement> &UnionStmt::getRightStmt() const
{
    return rightStmt;
}

void UnionStmt::setRightStmt(const std::shared_ptr<SQLStatement> &rightStmt)
{
    this->rightStmt = rightStmt;
}

bool UnionStmt::isUnionAll() const
{
    return unionAll;
}

void UnionStmt::setUnionAll(bool unionAll)
{
    this->unionAll = unionAll;
}
