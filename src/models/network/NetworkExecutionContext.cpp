#include "NetworkExecutionContext.h"

NetworkExecutionContext::NetworkExecutionContext()
    : isAuthorized(false)
{
}

const std::string &NetworkExecutionContext::getConnectionId() const
{
    return connectionId;
}

void NetworkExecutionContext::setConnectionId(const std::string &connectionId)
{
    this->connectionId = connectionId;
}

const std::string &NetworkExecutionContext::getCurrentUser() const
{
    return currentUser;
}

void NetworkExecutionContext::setCurrentUser(const std::string &currentUser)
{
    this->currentUser = currentUser;
}

const std::string &NetworkExecutionContext::getCurrentDbName() const
{
    return currentDbName;
}

void NetworkExecutionContext::setCurrentDbName(const std::string &currentDbName)
{
    this->currentDbName = currentDbName;
}

bool NetworkExecutionContext::getIsAuthorized() const
{
    return isAuthorized;
}

void NetworkExecutionContext::setIsAuthorized(bool isAuthorized)
{
    this->isAuthorized = isAuthorized;
}
