#include "SQLStatement.h"

SQLStatement::SQLStatement()
    : stmtType(StatementType::Unknown)
{
}

SQLStatement::SQLStatement(StatementType stmtType)
    : stmtType(stmtType)
{
}

SQLStatement::~SQLStatement() = default;

StatementType SQLStatement::getStmtType() const
{
    return stmtType;
}

void SQLStatement::setStmtType(StatementType stmtType)
{
    this->stmtType = stmtType;
}
