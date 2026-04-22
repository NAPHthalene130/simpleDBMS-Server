#include "NetworkManager.h"

#include "log/LogWriter.h"

NetworkManager::NetworkManager(Core *core)
    : core(core),
      netReceiver(new NetReceiver(core)),
      netSender(new NetSender(core)),
      clientSessionManager(new ClientSessionManager(core))
{
    LogWriter::info("network", "NetworkManager", "NetworkManager", "Network manager initialized.");
}

NetworkManager::~NetworkManager()
{
    LogWriter::info("network", "NetworkManager", "~NetworkManager", "Network manager is being destroyed.");
    stop();
    delete netReceiver;
    delete netSender;
    delete clientSessionManager;
    netReceiver = nullptr;
    netSender = nullptr;
    clientSessionManager = nullptr;
}

void NetworkManager::start()
{
    if (netReceiver != nullptr) {
        LogWriter::info("network", "NetworkManager", "start", "Starting network module.");
        netReceiver->start();
    }
}

void NetworkManager::stop()
{
    if (netReceiver != nullptr) {
        LogWriter::info("network", "NetworkManager", "stop", "Stopping network module.");
        netReceiver->stop();
    }
}

void NetworkManager::disconnected(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    // TODO: Handle session cleanup and connection lost notification.
    static_cast<void>(clientSocket);
    LogWriter::info("network", "NetworkManager", "disconnected", "Client disconnected from network module.");
}

NetReceiver *NetworkManager::getNetReceiver()
{
    return netReceiver;
}

NetSender *NetworkManager::getNetSender()
{
    return netSender;
}

ClientSessionManager *NetworkManager::getClientSessionManager()
{
    return clientSessionManager;
}
