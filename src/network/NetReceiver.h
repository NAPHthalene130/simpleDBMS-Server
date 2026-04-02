#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/thread_pool.hpp>

/**
 * @class NetReceiver
 * @brief 服务端网络接收服务类
 * @details 负责在独立线程中持续监听 10086 端口，并将客户端连接分发到并发工作线程池处理
 * @author NAPH130
 */
class NetReceiver
{
public:
    static constexpr unsigned short DEFAULT_LISTEN_PORT = 10086;

    /**
     * @brief 构造函数
     * @author NAPH130
     * @param listenPort 监听端口
     */
    explicit NetReceiver(unsigned short listenPort = DEFAULT_LISTEN_PORT);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~NetReceiver();

    /**
     * @brief 启动接收服务
     * @author NAPH130
     */
    void start();

    /**
     * @brief 停止接收服务
     * @author NAPH130
     */
    void stop();

    /**
     * @brief 处理最终接收到的完整消息
     * @author NAPH130
     * @param receiverStr 最终处理后的接收字符串
     */
    void receiveProcess(const std::string &receiverStr);

    /**
     * @brief 获取最近一次处理的消息内容
     * @author NAPH130
     * @return 最近一次处理的消息
     */
    std::string getLastReceivedMessage() const;

private:
    /**
     * @brief 执行监听服务主循环
     * @author NAPH130
     */
    void runService();

    /**
     * @brief 持续接受客户端连接
     * @author NAPH130
     */
    void acceptLoop();

    /**
     * @brief 处理单个客户端会话
     * @author NAPH130
     * @param clientSocket 客户端套接字
     */
    void handleClientSession(std::shared_ptr<asio::ip::tcp::socket> clientSocket);

    unsigned short listenPort;
    std::atomic<bool> isRunning;
    std::unique_ptr<asio::io_context> ioContext;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    std::unique_ptr<asio::thread_pool> workerPool;
    std::thread serviceThread;
    mutable std::mutex messageMutex;
    std::string lastReceivedMessage;
};
