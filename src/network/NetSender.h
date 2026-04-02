#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

/**
 * @class NetSender
 * @brief 服务端网络发送服务类
 * @details 负责以独立线程形式提供发送服务占位能力，后续可在此扩展响应回传逻辑
 * @author NAPH130
 */
class NetSender
{
public:
    static constexpr unsigned short DEFAULT_TARGET_PORT = 10086;

    /**
     * @brief 构造函数
     * @author NAPH130
     */
    NetSender();

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~NetSender();

    /**
     * @brief 启动发送服务线程
     * @author NAPH130
     */
    void start();

    /**
     * @brief 停止发送服务线程
     * @author NAPH130
     */
    void stop();

    /**
     * @brief 发送一条字符串消息
     * @author NAPH130
     * @param str 待发送的字符串内容
     */
    void send(const std::string &str);

private:
    /**
     * @brief 发送服务主循环
     * @author NAPH130
     */
    void runService();

    /**
     * @brief 真正执行网络发送
     * @author NAPH130
     * @param message 待发送消息
     * @param targetAddress 目标地址
     * @param targetPort 目标端口
     */
    void sendNow(const std::string &message, const std::string &targetAddress, unsigned short targetPort);

    struct PendingMessage {
        std::string message;
        std::string targetAddress;
        unsigned short targetPort;
    };

    std::atomic<bool> isRunning;
    std::thread serviceThread;
    std::mutex queueMutex;
    std::condition_variable conditionVariable;
    std::queue<PendingMessage> pendingMessages;
};
