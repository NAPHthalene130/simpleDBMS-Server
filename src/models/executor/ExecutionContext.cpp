#include "ExecutionContext.h"

ExecutionContext::ExecutionContext() = default;

const std::string &ExecutionContext::getCurrentDbName() const
{
    return currentDbName;
}

void ExecutionContext::setCurrentDbName(const std::string &currentDbName)
{
    this->currentDbName = currentDbName;
}

const std::string &ExecutionContext::getCurrentUser() const
{
    return currentUser;
}

void ExecutionContext::setCurrentUser(const std::string &currentUser)
{
    this->currentUser = currentUser;
}

const std::string &ExecutionContext::getConnectionId() const
{
    return connectionId;
}

void ExecutionContext::setConnectionId(const std::string &connectionId)
{
    this->connectionId = connectionId;
}
