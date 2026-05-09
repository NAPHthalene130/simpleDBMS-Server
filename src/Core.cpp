#include "Core.h"

#include "executor/ExecutorManager.h"
#include "log/LogWriter.h"
#include "network/NetworkManager.h"
#include "parser/ParserManager.h"
#include "storage/manager/StorageManager.h"
#include "tokenizer/Tokenizer.h"

Core::Core()
    : networkManager(new NetworkManager(this)),
      storageManager(new StorageManager(this)),
      executorManager(new ExecutorManager(this)),
      tokenizer(new Tokenizer(this)),
      parserManager(new ParserManager(this))
{
    LogWriter::info("core", "Core", "Core", "Core modules initialized.");
}

Core::~Core()
{
    LogWriter::info("core", "Core", "~Core", "Core is shutting down.");
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
    LogWriter::info("core", "Core", "~Core", "Core modules released.");
}

void Core::start()
{
    if (networkManager != nullptr) {
        LogWriter::info("core", "Core", "start", "Starting core services.");
        networkManager->start();
    }
}

void Core::stop()
{
    if (networkManager != nullptr) {
        LogWriter::info("core", "Core", "stop", "Stopping core services.");
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
