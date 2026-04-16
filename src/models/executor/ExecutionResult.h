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
 * @details 封装执行器返回给网络层的统一结果，包括状态、提示消息、影响行数与结果集。
 * @author NAPH130
 */
class ExecutionResult
{
public:
    ExecutionResult();

    ExecutionStatus getStatus() const;
    void setStatus(ExecutionStatus status);

    const std::string &getMessage() const;
    void setMessage(const std::string &message);

    std::int32_t getAffectedRows() const;
    void setAffectedRows(std::int32_t affectedRows);

    const std::vector<std::vector<std::string>> &getResultSet() const;
    void setResultSet(const std::vector<std::vector<std::string>> &resultSet);

private:
    ExecutionStatus status;
    std::string message;
    std::int32_t affectedRows;
    std::vector<std::vector<std::string>> resultSet;
};
