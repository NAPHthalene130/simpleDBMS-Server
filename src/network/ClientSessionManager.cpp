#include "ClientSessionManager.h"

#include <string>

#include "log/LogWriter.h"

ClientSessionManager::ClientSessionManager(Core *core)
    : core(core)
{
    LogWriter::info("network", "ClientSessionManager", "ClientSessionManager", "Client session manager initialized.");
}

void ClientSessionManager::addSession(asio::ip::tcp::socket *clientSocket)
{
    if (clientSocket == nullptr) {
        LogWriter::warning("network", "ClientSessionManager", "addSession", "Client socket is null.");
        return;
    }

    std::lock_guard<std::mutex> lock(sessionMutex);
    NetworkExecutionContext &networkExecutionContext = sessionMap[clientSocket];

    try {
        const asio::ip::tcp::endpoint remoteEndpoint = clientSocket->remote_endpoint();
        networkExecutionContext.setConnectionId(
            remoteEndpoint.address().to_string() + ":" + std::to_string(remoteEndpoint.port()));
        LogWriter::debug("network",
                         "ClientSessionManager",
                         "addSession",
                         "Session registered for " + networkExecutionContext.getConnectionId() + ".");
    } catch (...) {
        networkExecutionContext.setConnectionId("unknown");
        LogWriter::warning("network", "ClientSessionManager", "addSession", "Failed to resolve remote endpoint.");
    }
}

void ClientSessionManager::removeSession(asio::ip::tcp::socket *clientSocket)
{
    if (clientSocket == nullptr) {
        LogWriter::warning("network", "ClientSessionManager", "removeSession", "Client socket is null.");
        return;
    }

    std::lock_guard<std::mutex> lock(sessionMutex);
    sessionMap.erase(clientSocket);
    LogWriter::debug("network", "ClientSessionManager", "removeSession", "Session removed for disconnected client.");
}

NetworkExecutionContext *ClientSessionManager::findSessionContext(asio::ip::tcp::socket *clientSocket)
{
    std::lock_guard<std::mutex> lock(sessionMutex);
    const auto iterator = sessionMap.find(clientSocket);
    if (iterator == sessionMap.end()) {
        LogWriter::warning("network", "ClientSessionManager", "findSessionContext", "Session context was not found.");
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
