#include "NetSender.h"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <asio/connect.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

namespace {

std::string buildLogMessage(const std::string &level, const std::string &message)
{
    const std::time_t currentTime = std::time(nullptr);
    std::tm localTime{};
    localtime_s(&localTime, &currentTime);

    std::ostringstream outputStream;
    outputStream << '[' << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << ']'
                 << '[' << level << "] "
                 << message;
    return outputStream.str();
}

}

NetSender::NetSender()
    : isRunning(false)
{
}

NetSender::~NetSender()
{
    stop();
}

void NetSender::start()
{
    if (isRunning.exchange(true)) {
        return;
    }

    serviceThread = std::thread(&NetSender::runService, this);
}

void NetSender::stop()
{
    if (!isRunning.exchange(false)) {
        return;
    }

    conditionVariable.notify_all();

    if (serviceThread.joinable()) {
        serviceThread.join();
    }
}

void NetSender::send(const std::string &str)
{
    if (str.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pendingMessages.push(PendingMessage{str, "127.0.0.1", DEFAULT_TARGET_PORT});
    }

    conditionVariable.notify_one();
}

void NetSender::runService()
{
    while (true) {
        PendingMessage pendingMessage;

        {
            std::unique_lock<std::mutex> lock(queueMutex);
            conditionVariable.wait(lock,
                                   [this]() {
                                       return !isRunning.load() || !pendingMessages.empty();
                                   });

            if (!isRunning.load() && pendingMessages.empty()) {
                return;
            }

            pendingMessage = pendingMessages.front();
            pendingMessages.pop();
        }

        sendNow(pendingMessage.message, pendingMessage.targetAddress, pendingMessage.targetPort);
    }
}

void NetSender::sendNow(const std::string &message,
                        const std::string &targetAddress,
                        unsigned short targetPort)
{
    try {
        asio::io_context ioContext;
        asio::ip::tcp::resolver resolver(ioContext);
        asio::ip::tcp::socket socket(ioContext);
        const auto endpoints = resolver.resolve(targetAddress, std::to_string(targetPort));

        asio::connect(socket, endpoints);
        asio::write(socket, asio::buffer(message));
    } catch (const std::exception &exception) {
        std::cerr << buildLogMessage("ERROR", "NetSender::sendNow failed: " + std::string(exception.what()))
                  << std::endl;
    }
}
