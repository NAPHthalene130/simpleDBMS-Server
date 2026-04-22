#include "Core.h"

#include "log/LogWriter.h"
#include "executor/ExecutorManager.h"
#include "network/NetworkManager.h"
#include "core/SqlPipeline.h"
#include "storage/manager/StorageManager.h"
#include "tokenizer/Tokenizer.h"
#include "parser/ParserManager.h"

Core::Core()
    : networkManager(new NetworkManager(this)),
      storageManager(new StorageManager(this)),
      executorManager(new ExecutorManager(this)),
      tokenizer(new Tokenizer(this)),
      parserManager(new ParserManager(this)),
      sqlPipeline(new SqlPipeline(this))
{
    LogWriter::info("core", "Core", "Core", "Core modules initialized.");
}

Core::~Core()
{
    LogWriter::info("core", "Core", "~Core", "Core is shutting down.");
    stop();
    delete sqlPipeline;
    delete parserManager;
    delete tokenizer;
    delete networkManager;
    delete storageManager;
    delete executorManager;
    sqlPipeline = nullptr;
    parserManager = nullptr;
    tokenizer = nullptr;
    networkManager = nullptr;
    storageManager = nullptr;
    executorManager = nullptr;
    LogWriter::info("core", "Core", "~Core", "Core modules released.");
}

void Core::start()
{
    LogWriter::info("core", "Core", "start", "Starting core services.");
    if (networkManager != nullptr) {
        networkManager->start();
        LogWriter::info("core", "Core", "start", "Network manager started.");
        return;
    }

    LogWriter::warning("core", "Core", "start", "Network manager is null, startup skipped.");
}

void Core::stop()
{
    LogWriter::info("core", "Core", "stop", "Stopping core services.");
    if (networkManager != nullptr) {
        networkManager->stop();
        LogWriter::info("core", "Core", "stop", "Network manager stopped.");
        return;
    }

    LogWriter::warning("core", "Core", "stop", "Network manager is null, stop skipped.");
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

/**
 * @brief 获取 SQL 编排服务
 * @author YuzhSong
 * @return SQL 编排服务指针
 */
SqlPipeline *Core::getSqlPipeline()
{
    return sqlPipeline;
}
