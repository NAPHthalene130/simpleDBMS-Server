#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @class TableNode
 * @brief 目录中的数据表节点
 * @details 用于表达目录结构中的表名称与字段列表。
 * @author Qi
 */
class TableNode
{
public:
    TableNode();
    TableNode(const std::string &name, const std::vector<std::string> &fields);

    const std::string &getName() const;
    void setName(const std::string &name);

    const std::vector<std::string> &getFields() const;
    void setFields(const std::vector<std::string> &fields);

    std::string toJson() const;
    static TableNode fromJson(const std::string &jsonStr);

private:
    std::string name;
    std::vector<std::string> fields;
};

/**
 * @class DatabaseNode
 * @brief 目录中的数据库节点
 * @details 用于表达目录结构中的数据库名称与下属表节点。
 * @author Qi
 */
class DatabaseNode
{
public:
    DatabaseNode();
    DatabaseNode(const std::string &name, const std::vector<TableNode> &tables);

    const std::string &getName() const;
    void setName(const std::string &name);

    const std::vector<TableNode> &getTables() const;
    void setTables(const std::vector<TableNode> &tables);

    std::uint64_t getDbVersion() const;
    void setDbVersion(std::uint64_t dbVersion);

    std::string toJson() const;
    static DatabaseNode fromJson(const std::string &jsonStr);

private:
    std::string name;
    std::vector<TableNode> tables;
    std::uint64_t dbVersion = 0;
};

/**
 * @class NetworkTransferData
 * @brief 网络传输业务数据类
 * @details 该类仅用于网络业务报文存储与 JSON 序列化，不承担 UI、网络发送与 SQL 执行逻辑。
 * @author Qi
 */
class NetworkTransferData
{
public:
    inline static const std::string LOGIN_REQUEST = "LOGIN_REQUEST";
    inline static const std::string LOGIN_RESPONSE = "LOGIN_RESPONSE";
    inline static const std::string VERIFY_REQUEST = "VERIFY_REQUEST";
    inline static const std::string VERIFY_RESPONSE = "VERIFY_RESPONSE";
    inline static const std::string USE_DATABASE_REQUEST = "USE_DATABASE_REQUEST";
    inline static const std::string USE_DATABASE_RESPONSE = "USE_DATABASE_RESPONSE";
    inline static const std::string SQL_EXEC_REQUEST = "SQL_EXEC_REQUEST";
    inline static const std::string SQL_EXEC_RESPONSE = "SQL_EXEC_RESPONSE";
    inline static const std::string SQL_QUERY_RESPONSE = "SQL_QUERY_RESPONSE";
    inline static const std::string DIRECTORY_REQUEST = "DIRECTORY_REQUEST";
    inline static const std::string DIRECTORY_RESPONSE = "DIRECTORY_RESPONSE";
    inline static const std::string ERROR_RESPONSE = "ERROR_RESPONSE";
    inline static const std::string DB_VERSION_REQUEST = "DB_VERSION_REQUEST";
    inline static const std::string DB_VERSION_RESPONSE = "DB_VERSION_RESPONSE";

    NetworkTransferData();
    NetworkTransferData(const std::string &type, const std::string &id);

    std::string toJson() const;
    static NetworkTransferData fromJson(const std::string &jsonStr);

    const std::string &getType() const;
    void setType(const std::string &type);

    const std::string &getId() const;
    void setId(const std::string &id);

    const std::string &getPassword() const;
    void setPassword(const std::string &password);

    const std::string &getDbName() const;
    void setDbName(const std::string &dbName);

    const std::string &getSql() const;
    void setSql(const std::string &sql);

    bool getSuccess() const;
    void setSuccess(bool success);

    const std::string &getMessage() const;
    void setMessage(const std::string &message);

    int getAffectedRows() const;
    void setAffectedRows(int affectedRows);

    const std::vector<std::string> &getColumns() const;
    void setColumns(const std::vector<std::string> &columns);

    const std::vector<std::vector<std::string>> &getRows() const;
    void setRows(const std::vector<std::vector<std::string>> &rows);

    const std::vector<DatabaseNode> &getDatabases() const;
    void setDatabases(const std::vector<DatabaseNode> &databases);

    std::uint64_t getDbVersion() const;
    void setDbVersion(std::uint64_t dbVersion);

private:
    std::string type;
    std::string id;
    std::string password;
    std::string dbName;
    std::string sql;
    bool success;
    std::string message;
    int affectedRows;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    std::vector<DatabaseNode> databases;
    std::uint64_t dbVersion = 0;
};