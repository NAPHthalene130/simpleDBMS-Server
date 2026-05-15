#pragma once

#include <memory>
#include <string>

#include "SQLStatement.h"

class SelectStmt;

/**
 * @class UnionStmt
 * @brief UNION 查询语句数据类
 * @details 封装 UNION/UNION ALL 语句，包含两个子 SELECT 语句。
 * @author NAPH130
 */
class UnionStmt : public SQLStatement {
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param unionAll 是否为 UNION ALL
     */
    explicit UnionStmt(bool unionAll = false);

    const std::shared_ptr<SQLStatement> &getLeftStmt() const;
    void setLeftStmt(const std::shared_ptr<SQLStatement> &leftStmt);

    const std::shared_ptr<SQLStatement> &getRightStmt() const;
    void setRightStmt(const std::shared_ptr<SQLStatement> &rightStmt);

    bool isUnionAll() const;
    void setUnionAll(bool unionAll);

private:
    std::shared_ptr<SQLStatement> leftStmt;
    std::shared_ptr<SQLStatement> rightStmt;
    bool unionAll;
};
