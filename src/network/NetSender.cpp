#include "NetSender.h"

#include <iostream>

#include <asio/write.hpp>

NetSender::NetSender(Core *core)
    : core(core)
{
}

NetSender::~NetSender() = default;

void NetSender::send(std::shared_ptr<asio::ip::tcp::socket> clientSocket, const std::string &msg)
{
    if (clientSocket == nullptr || !clientSocket->is_open() || msg.empty()) {
        return;
    }

    try {
        const std::array<unsigned char, 4> lengthHeader = buildLengthHeader(static_cast<std::uint32_t>(msg.size()));
        asio::write(*clientSocket, asio::buffer(lengthHeader));
        asio::write(*clientSocket, asio::buffer(msg));
    } catch (const std::exception &exception) {
        std::cerr << "NetSender::send failed: " << exception.what() << std::endl;
    }
}

std::array<unsigned char, 4> NetSender::buildLengthHeader(std::uint32_t messageLength) const
{
    return {
        static_cast<unsigned char>((messageLength >> 24) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 16) & 0xFFU),
        static_cast<unsigned char>((messageLength >> 8) & 0xFFU),
        static_cast<unsigned char>(messageLength & 0xFFU)};
}
