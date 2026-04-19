#include "ClientSessionManager.h"

ClientSessionManager::ClientSessionManager(Core *core)
    : core(core)
{
}

void ClientSessionManager::addSession(asio::ip::tcp::socket *clientSocket)
{
    // TODO: Initialize session context after authentication flow is defined.
    static_cast<void>(clientSocket);
}

void ClientSessionManager::removeSession(asio::ip::tcp::socket *clientSocket)
{
    // TODO: Remove session state when lifecycle management is finalized.
    static_cast<void>(clientSocket);
}

NetworkExecutionContext *ClientSessionManager::findSessionContext(asio::ip::tcp::socket *clientSocket)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    const auto iterator = sessionMap.find(clientSocket);
    if (iterator == sessionMap.end()) {
        return nullptr;
    }

    return &iterator->second;
}

bool ClientSessionManager::hasSession(asio::ip::tcp::socket *clientSocket) const
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    return sessionMap.find(clientSocket) != sessionMap.end();
}

const std::map<asio::ip::tcp::socket *, NetworkExecutionContext> *ClientSessionManager::getSessionMap() const
{
    return &sessionMap;
}
