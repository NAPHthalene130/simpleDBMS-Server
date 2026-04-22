#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * @enum ExecutionStatus
 * @brief 执行状态枚举
 * @details 用于标识 SQL 执行结果的成功或失败状态。
 * @author NAPH130
 */
enum class ExecutionStatus
{
    Success,
    Failure
};

/**
 * @class ExecutionResult
 * @brief 执行结果数据类
 * @details 封装执行器返回给网络层的统一结果，包括状态、提示消息、影响行数、结果集、
 *          关联数据库名、关联表名，以及 JSON 序列化与反序列化能力。
 * @author NAPH130
 */
class ExecutionResult
{
public:
    /**
     * @brief 默认构造函数
     * @author NAPH130
     */
    ExecutionResult();

    /**
     * @brief 将执行结果序列化为 JSON 字符串
     * @author NAPH130
     * @return JSON 格式的执行结果字符串
     */
    std::string toJson() const;

    /**
     * @brief 从 JSON 字符串反序列化执行结果对象
     * @author NAPH130
     * @param jsonStr JSON 格式的执行结果字符串
     * @return 反序列化后的执行结果对象
     */
    static ExecutionResult fromJson(const std::string &jsonStr);

    ExecutionStatus getStatus() const;
    void setStatus(ExecutionStatus status);

    const std::string &getMessage() const;
    void setMessage(const std::string &message);

    std::int32_t getAffectedRows() const;
    void setAffectedRows(std::int32_t affectedRows);

    const std::vector<std::vector<std::string>> &getResultSet() const;
    void setResultSet(const std::vector<std::vector<std::string>> &resultSet);

    const std::string &getDbName() const;
    void setDbName(const std::string &dbName);

    const std::string &getTableName() const;
    void setTableName(const std::string &tableName);

private:
    ExecutionStatus status;
    std::string message;
    std::int32_t affectedRows;
    std::vector<std::vector<std::string>> resultSet;
    std::string dbName;
    std::string tableName;
};
