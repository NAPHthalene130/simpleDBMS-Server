#include "ExecutionResult.h"

#include <nlohmann/json.hpp>

namespace {
std::string executionStatusToString(ExecutionStatus status)
{
    return status == ExecutionStatus::Success ? "Success" : "Failure";
}

ExecutionStatus executionStatusFromString(const std::string &status)
{
    return status == "Success" ? ExecutionStatus::Success : ExecutionStatus::Failure;
}
}

ExecutionResult::ExecutionResult()
    : status(ExecutionStatus::Failure), affectedRows(0)
{
}

std::string ExecutionResult::toJson() const
{
    nlohmann::json jsonObject;
    jsonObject["status"] = executionStatusToString(status);
    jsonObject["message"] = message;
    jsonObject["affectedRows"] = affectedRows;
    jsonObject["resultSet"] = resultSet;
    jsonObject["dbName"] = dbName;
    jsonObject["tableName"] = tableName;
    return jsonObject.dump();
}

ExecutionResult ExecutionResult::fromJson(const std::string &jsonStr)
{
    const nlohmann::json jsonObject = nlohmann::json::parse(jsonStr);

    ExecutionResult executionResult;
    executionResult.setStatus(executionStatusFromString(jsonObject.value("status", "Failure")));
    executionResult.setMessage(jsonObject.value("message", ""));
    executionResult.setAffectedRows(jsonObject.value("affectedRows", 0));
    executionResult.setResultSet(jsonObject.value("resultSet", std::vector<std::vector<std::string>>{}));
    executionResult.setDbName(jsonObject.value("dbName", ""));
    executionResult.setTableName(jsonObject.value("tableName", ""));
    return executionResult;
}

ExecutionStatus ExecutionResult::getStatus() const
{
    return status;
}

void ExecutionResult::setStatus(ExecutionStatus status)
{
    this->status = status;
}

const std::string &ExecutionResult::getMessage() const
{
    return message;
}

void ExecutionResult::setMessage(const std::string &message)
{
    this->message = message;
}

std::int32_t ExecutionResult::getAffectedRows() const
{
    return affectedRows;
}

void ExecutionResult::setAffectedRows(std::int32_t affectedRows)
{
    this->affectedRows = affectedRows;
}

const std::vector<std::vector<std::string>> &ExecutionResult::getResultSet() const
{
    return resultSet;
}

void ExecutionResult::setResultSet(const std::vector<std::vector<std::string>> &resultSet)
{
    this->resultSet = resultSet;
}

const std::string &ExecutionResult::getDbName() const
{
    return dbName;
}

void ExecutionResult::setDbName(const std::string &dbName)
{
    this->dbName = dbName;
}

const std::string &ExecutionResult::getTableName() const
{
    return tableName;
}

void ExecutionResult::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}
