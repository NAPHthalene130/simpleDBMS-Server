#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <asio/ip/tcp.hpp>

class Core;

/**
 * @class NetSender
 * @brief 服务端网络发送服务类
 * @details 负责向指定客户端套接字发送长度前缀协议消息。
 * @author NAPH130
 */
class NetSender
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象
     */
    explicit NetSender(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~NetSender();

    /**
     * @brief 发送一条字符串消息
     * @author NAPH130
     * @param clientSocket 客户端套接字
     * @param msg 待发送的消息内容
     */
    void send(std::shared_ptr<asio::ip::tcp::socket> clientSocket, const std::string &msg);

private:
    /**
     * @brief 构建 4 字节长度前缀
     * @author NAPH130
     * @param messageLength 消息长度
     * @return 长度前缀字节数组
     */
    std::array<unsigned char, 4> buildLengthHeader(std::uint32_t messageLength) const;

private:
    Core *core;
};
