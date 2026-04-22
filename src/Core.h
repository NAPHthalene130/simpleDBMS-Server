#pragma once

class NetworkManager;
class ExecutorManager;
class StorageManager;
class Tokenizer;
class ParserManager;
class SqlPipeline;

/**
 * @class Core
 * @brief 服务端核心调度类
 * @details 负责统一管理网络接收与发送服务的生命周期
 * @author NAPH130
 */
class Core
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     */
    Core();

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~Core();

    /**
     * @brief 启动核心服务
     * @author NAPH130
     */
    void start();

    /**
     * @brief 停止核心服务
     * @author NAPH130
     */
    void stop();

    /**
     * @brief 获取网络管理器
     * @author NAPH130
     * @return 网络管理器指针
     */
    NetworkManager *getNetworkManager();
    ExecutorManager *getExecutorManager();
    StorageManager *getStorageManager();
    Tokenizer *getTokenizer();
    ParserManager *getParserManager();
    /**
     * @brief 获取 SQL 编排服务
     * @author YuzhSong
     * @return SQL 编排服务指针
     */
    SqlPipeline *getSqlPipeline();

private:
    NetworkManager *networkManager;
    StorageManager *storageManager;
    ExecutorManager *executorManager;
    Tokenizer *tokenizer;
    ParserManager *parserManager;
    SqlPipeline *sqlPipeline;
};
