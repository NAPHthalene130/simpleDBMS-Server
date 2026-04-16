#include "ExecutionResult.h"

ExecutionResult::ExecutionResult()
    : status(ExecutionStatus::Failure), affectedRows(0)
{
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
