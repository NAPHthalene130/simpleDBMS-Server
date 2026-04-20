#include "Core.h"

#include "executor/ExecutorManager.h"
#include "network/NetworkManager.h"
#include "storage/manager/StorageManager.h"

Core::Core()
    : networkManager(new NetworkManager(this)),
      storageManager(new StorageManager(this)),
      executorManager(new ExecutorManager(this))
{
}

Core::~Core()
{
    stop();
    delete networkManager;
    delete storageManager;
    delete executorManager;
    networkManager = nullptr;
    storageManager = nullptr;
    executorManager = nullptr;
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

ExecutorManager *Core::getExecutorManager()
{
    return executorManager;
}

StorageManager *Core::getStorageManager()
{
    return storageManager;
}
