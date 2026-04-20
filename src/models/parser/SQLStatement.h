#pragma once

#include <string>

#include "models/executor/ExecutionStatementType.h"

/**
 * @class SQLStatement
 * @brief SQL 语句基类
 * @details 作为所有 SQL 语句数据类的统一父类，供执行器按语句类型分发处理。
 * @author NAPH130
 */
class SQLStatement
{
public:
    SQLStatement();
    explicit SQLStatement(ExecutionStatementType stmtType);
    virtual ~SQLStatement();

    ExecutionStatementType getStmtType() const;
    void setStmtType(ExecutionStatementType stmtType);

private:
    ExecutionStatementType stmtType;
};
