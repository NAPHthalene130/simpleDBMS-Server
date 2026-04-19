#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/thread_pool.hpp>

class Core;

/**
 * @class NetReceiver
 * @brief 服务端网络接收服务类
 * @details 负责监听客户端连接，并按“4 字节长度 + 消息体”的协议读取完整消息。
 * @author NAPH130
 */
class NetReceiver
{
public:
    static constexpr unsigned short DEFAULT_LISTEN_PORT = 10086;

    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象
     * @param listenPort 监听端口
     */
    explicit NetReceiver(Core *core, unsigned short listenPort = DEFAULT_LISTEN_PORT);

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
     * @param clientSocket 消息来源客户端套接字
     * @param msg 最终处理后的接收字符串
     */
    void processMsg(std::shared_ptr<asio::ip::tcp::socket> clientSocket, const std::string &msg);

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

    /**
     * @brief 注册活动客户端套接字
     * @author NAPH130
     * @param clientSocket 客户端套接字
     */
    void addActiveSocket(std::shared_ptr<asio::ip::tcp::socket> clientSocket);

    /**
     * @brief 移除活动客户端套接字
     * @author NAPH130
     * @param clientSocket 客户端套接字
     */
    void removeActiveSocket(std::shared_ptr<asio::ip::tcp::socket> clientSocket);

    /**
     * @brief 解析 4 字节长度前缀
     * @author NAPH130
     * @param lengthHeader 长度前缀字节数组
     * @return 消息体长度
     */
    std::uint32_t parseLengthHeader(const std::array<unsigned char, 4> &lengthHeader) const;

    Core *core;
    unsigned short listenPort;
    std::atomic<bool> isRunning;
    std::unique_ptr<asio::io_context> ioContext;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor;
    std::unique_ptr<asio::thread_pool> workerPool;
    std::thread serviceThread;
    mutable std::mutex messageMutex;
    mutable std::mutex socketMutex;
    std::vector<std::shared_ptr<asio::ip::tcp::socket>> activeClientSockets;
    std::string lastReceivedMessage;
};
