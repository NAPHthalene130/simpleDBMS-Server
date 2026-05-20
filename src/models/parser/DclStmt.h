#pragma once

#include <string>
#include <vector>

#include "SQLStatement.h"

/**
 * @enum DclOperationType
 * @brief DCL 操作类型枚举
 * @details 区分 GRANT 与 REVOKE 两种数据控制操作。
 * @author NAPH130
 */
enum class DclOperationType
{
    Grant,   ///< 授予权限
    Revoke   ///< 撤销权限
};

/**
 * @class DclStmt
 * @brief 数据控制语言语句数据类
 * @details 封装 GRANT 或 REVOKE 语句解析后的操作类型、用户标识及密码信息。
 *          当前阶段仅支持 GRANT/REVOKE ALL PRIVILEGES。
 * @author NAPH130
 */
class DclStmt : public SQLStatement
{
public:
    DclStmt();

    DclOperationType getOperationType() const;
    void setOperationType(DclOperationType operationType);

    const std::string &getUserName() const;
    void setUserName(const std::string &userName);

    const std::string &getPassword() const;
    void setPassword(const std::string &password);

private:
    DclOperationType operationType;
    std::string userName;
    std::string password;
};
