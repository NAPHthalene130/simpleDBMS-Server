#include "SQLStatement.h"

SQLStatement::SQLStatement()
    : stmtType(ExecutionStatementType::Unknown)
{
}

SQLStatement::SQLStatement(ExecutionStatementType stmtType)
    : stmtType(stmtType)
{
}

SQLStatement::~SQLStatement() = default;

ExecutionStatementType SQLStatement::getStmtType() const
{
    return stmtType;
}

void SQLStatement::setStmtType(ExecutionStatementType stmtType)
{
    this->stmtType = stmtType;
}
