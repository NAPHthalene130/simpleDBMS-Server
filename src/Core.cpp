#include "Core.h"

#include "executor/ExecutorManager.h"
#include "network/NetworkManager.h"
#include "storage/manager/StorageManager.h"
#include "tokenizer/Tokenizer.h"
#include "parser/ParserManager.h"

Core::Core()
    : networkManager(new NetworkManager(this)),
      storageManager(new StorageManager(this)),
      executorManager(new ExecutorManager(this)),
      tokenizer(new Tokenizer(this)),
      parserManager(new ParserManager(this))
{
}

Core::~Core()
{
    stop();
    delete parserManager;
    delete tokenizer;
    delete networkManager;
    delete storageManager;
    delete executorManager;
    parserManager = nullptr;
    tokenizer = nullptr;
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

Tokenizer *Core::getTokenizer()
{
    return tokenizer;
}

ParserManager *Core::getParserManager()
{
    return parserManager;
}
