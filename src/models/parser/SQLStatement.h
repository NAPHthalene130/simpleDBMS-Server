#pragma once

#include <string>

/**
 * @enum StatementType
 * @brief SQL 语句类型枚举
 * @details 用于标识抽象语法树节点对应的语句类别。
 * @author NAPH130
 */
enum class StatementType
{
    CreateDatabase,
    CreateTable,
    Insert,
    Select,
    Unknown
};

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
    explicit SQLStatement(StatementType stmtType);
    virtual ~SQLStatement();

    StatementType getStmtType() const;
    void setStmtType(StatementType stmtType);

private:
    StatementType stmtType;
};
