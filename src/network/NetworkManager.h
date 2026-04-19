#pragma once

#include <asio/ip/tcp.hpp>

#include "ClientSessionManager.h"
#include "NetReceiver.h"
#include "NetSender.h"

class Core;

/**
 * @class NetworkManager
 * @brief 服务端网络管理器
 * @details 统一管理网络接收器、发送器与客户端会话管理器，作为网络模块的中控入口。
 * @author NAPH130
 */
class NetworkManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象
     */
    explicit NetworkManager(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~NetworkManager();

    /**
     * @brief 启动网络模块
     * @author NAPH130
     */
    void start();

    /**
     * @brief 停止网络模块
     * @author NAPH130
     */
    void stop();

    /**
     * @brief 处理客户端断开连接事件
     * @author NAPH130
     * @param clientSocket 丢失连接的客户端套接字
     */
    void disconnected(std::shared_ptr<asio::ip::tcp::socket> clientSocket);

    /**
     * @brief 获取网络接收器
     * @author NAPH130
     * @return 网络接收器指针
     */
    NetReceiver *getNetReceiver();

    /**
     * @brief 获取网络发送器
     * @author NAPH130
     * @return 网络发送器指针
     */
    NetSender *getNetSender();

    /**
     * @brief 获取客户端会话管理器
     * @author NAPH130
     * @return 客户端会话管理器指针
     */
    ClientSessionManager *getClientSessionManager();

private:
    Core *core;
    NetReceiver *netReceiver;
    NetSender *netSender;
    ClientSessionManager *clientSessionManager;
};
