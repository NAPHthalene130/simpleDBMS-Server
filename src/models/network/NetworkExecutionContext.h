#pragma once

#include <string>

/**
 * @class NetworkExecutionContext
 * @brief 网络执行上下文数据类
 * @details 封装客户端连接对应的会话信息，包括连接标识、当前用户、当前数据库与鉴权状态。
 * @author NAPH130
 */
class NetworkExecutionContext
{
public:
    /**
     * @brief 默认构造函数
     * @author NAPH130
     */
    NetworkExecutionContext();

    /**
     * @brief 获取连接唯一标识
     * @author NAPH130
     * @return 连接唯一标识
     */
    const std::string &getConnectionId() const;

    /**
     * @brief 设置连接唯一标识
     * @author NAPH130
     * @param connectionId 连接唯一标识
     */
    void setConnectionId(const std::string &connectionId);

    /**
     * @brief 获取当前登录用户名
     * @author NAPH130
     * @return 当前登录用户名
     */
    const std::string &getCurrentUser() const;

    /**
     * @brief 设置当前登录用户名
     * @author NAPH130
     * @param currentUser 当前登录用户名
     */
    void setCurrentUser(const std::string &currentUser);

    /**
     * @brief 获取当前使用的数据库名
     * @author NAPH130
     * @return 当前使用的数据库名
     */
    const std::string &getCurrentDbName() const;

    /**
     * @brief 设置当前使用的数据库名
     * @author NAPH130
     * @param currentDbName 当前使用的数据库名
     */
    void setCurrentDbName(const std::string &currentDbName);

    /**
     * @brief 获取当前连接是否已通过鉴权
     * @author NAPH130
     * @return 是否已鉴权
     */
    bool getIsAuthorized() const;

    /**
     * @brief 设置当前连接是否已通过鉴权
     * @author NAPH130
     * @param isAuthorized 是否已鉴权
     */
    void setIsAuthorized(bool isAuthorized);

private:
    std::string connectionId;
    std::string currentUser;
    std::string currentDbName;
    bool isAuthorized;
};
