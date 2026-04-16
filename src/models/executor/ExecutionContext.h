#pragma once

#include <string>

/**
 * @class ExecutionContext
 * @brief 执行上下文数据类
 * @details 封装当前客户端连接的数据库环境、登录用户与连接标识等执行上下文信息。
 * @author NAPH130
 */
class ExecutionContext
{
public:
    ExecutionContext();

    const std::string &getCurrentDbName() const;
    void setCurrentDbName(const std::string &currentDbName);

    const std::string &getCurrentUser() const;
    void setCurrentUser(const std::string &currentUser);

    const std::string &getConnectionId() const;
    void setConnectionId(const std::string &connectionId);

private:
    std::string currentDbName;
    std::string currentUser;
    std::string connectionId;
};
