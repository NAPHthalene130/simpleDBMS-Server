#include "Core.h"

#include "network/NetworkManager.h"

Core::Core()
    : networkManager(new NetworkManager(this))
{
}

Core::~Core()
{
    stop();
    delete networkManager;
    networkManager = nullptr;
}

void Core::start()
{
    if (networkManager != nullptr) {
        networkManager->start();
    }
}

void Core::stop()
{
    if (networkManager != nullptr) {
        networkManager->stop();
    }
}

NetworkManager *Core::getNetworkManager()
{
    return networkManager;
}
