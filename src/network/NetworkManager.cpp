#include "NetworkManager.h"

NetworkManager::NetworkManager(Core *core)
    : core(core),
      netReceiver(new NetReceiver(core)),
      netSender(new NetSender(core)),
      clientSessionManager(new ClientSessionManager(core))
{
}

NetworkManager::~NetworkManager()
{
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
        netReceiver->start();
    }
}

void NetworkManager::stop()
{
    if (netReceiver != nullptr) {
        netReceiver->stop();
    }
}

void NetworkManager::disconnected(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    // TODO: Handle session cleanup and connection lost notification.
    static_cast<void>(clientSocket);
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
