#pragma once

#include <cstdint>
#include <string>

/**
 * @class SqlData
 * @brief SQL 请求数据类
 * @details 封装用户侧或客户端侧发送给服务端的 SQL 请求上下文信息。
 * @author NAPH130
 */
class SqlData
{
public:
    /**
     * @brief 默认构造函数
     * @author NAPH130
     */
    SqlData();

    /**
     * @brief 带参构造函数
     * @author NAPH130
     * @param userID 用户标识
     * @param dbName 数据库名称
     * @param dbVersion 数据库版本号
     * @param sql SQL 语句内容
     */
    SqlData(const std::string &userID,
            const std::string &dbName,
            std::uint64_t dbVersion,
            const std::string &sql);

    /**
     * @brief 将当前对象序列化为 JSON 字符串
     * @author NAPH130
     * @return JSON 格式字符串
     */
    std::string toJson() const;

    /**
     * @brief 从 JSON 字符串反序列化对象
     * @author NAPH130
     * @param jsonStr JSON 格式字符串
     * @return 反序列化后的 SQL 请求数据对象
     */
    static SqlData fromJson(const std::string &jsonStr);

    /**
     * @brief 获取用户标识
     * @author NAPH130
     * @return 用户标识
     */
    const std::string &getUserID() const;

    /**
     * @brief 设置用户标识
     * @author NAPH130
     * @param userID 用户标识
     */
    void setUserID(const std::string &userID);

    /**
     * @brief 获取数据库名称
     * @author NAPH130
     * @return 数据库名称
     */
    const std::string &getDbName() const;

    /**
     * @brief 设置数据库名称
     * @author NAPH130
     * @param dbName 数据库名称
     */
    void setDbName(const std::string &dbName);

    /**
     * @brief 获取数据库版本号
     * @author NAPH130
     * @return 数据库版本号
     */
    std::uint64_t getDbVersion() const;

    /**
     * @brief 设置数据库版本号
     * @author NAPH130
     * @param dbVersion 数据库版本号
     */
    void setDbVersion(std::uint64_t dbVersion);

    /**
     * @brief 获取 SQL 语句内容
     * @author NAPH130
     * @return SQL 语句内容
     */
    const std::string &getSql() const;

    /**
     * @brief 设置 SQL 语句内容
     * @author NAPH130
     * @param sql SQL 语句内容
     */
    void setSql(const std::string &sql);

private:
    std::string userID;
    std::string dbName;
    std::uint64_t dbVersion;
    std::string sql;
};
