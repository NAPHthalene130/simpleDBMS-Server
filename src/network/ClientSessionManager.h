#pragma once

#include <map>
#include <mutex>

#include <asio/ip/tcp.hpp>

#include "models/network/NetworkExecutionContext.h"

class Core;

/**
 * @class ClientSessionManager
 * @brief 客户端会话管理器
 * @details 管理客户端套接字与网络执行上下文之间的映射关系，并为后续会话控制预留接口。
 * @author NAPH130
 */
class ClientSessionManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象
     */
    explicit ClientSessionManager(Core *core);

    /**
     * @brief 注册客户端会话
     * @author NAPH130
     * @param clientSocket 客户端套接字
     */
    void addSession(asio::ip::tcp::socket *clientSocket);

    /**
     * @brief 移除客户端会话
     * @author NAPH130
     * @param clientSocket 客户端套接字
     */
    void removeSession(asio::ip::tcp::socket *clientSocket);

    /**
     * @brief 查找客户端会话上下文
     * @author NAPH130
     * @param clientSocket 客户端套接字
     * @return 会话上下文指针
     */
    NetworkExecutionContext *findSessionContext(asio::ip::tcp::socket *clientSocket);

    /**
     * @brief 检查客户端会话是否存在
     * @author NAPH130
     * @param clientSocket 客户端套接字
     * @return 会话是否存在
     */
    bool hasSession(asio::ip::tcp::socket *clientSocket) const;

    /**
     * @brief 获取当前所有会话映射
     * @author NAPH130
     * @return 会话映射表
     */
    const std::map<asio::ip::tcp::socket *, NetworkExecutionContext> *getSessionMap() const;

private:
    Core *core;
    mutable std::mutex sessionMutex;
    std::map<asio::ip::tcp::socket *, NetworkExecutionContext> sessionMap;
};
