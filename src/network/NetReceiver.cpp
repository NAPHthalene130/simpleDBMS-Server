#include "NetReceiver.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <system_error>

NetReceiver::NetReceiver(unsigned short listenPort)
    : listenPort(listenPort),
      isRunning(false)
{
}

NetReceiver::~NetReceiver()
{
    stop();
}

void NetReceiver::start()
{
    if (isRunning.exchange(true)) {
        return;
    }

    serviceThread = std::thread(&NetReceiver::runService, this);
}

void NetReceiver::stop()
{
    if (!isRunning.exchange(false)) {
        return;
    }

    if (acceptor != nullptr) {
        std::error_code errorCode;
        acceptor->close(errorCode);
    }

    if (ioContext != nullptr) {
        ioContext->stop();
    }

    if (serviceThread.joinable()) {
        serviceThread.join();
    }

    if (workerPool != nullptr) {
        workerPool->join();
        workerPool.reset();
    }

    acceptor.reset();
    ioContext.reset();
}

void NetReceiver::receiveProcess(const std::string &receiverStr)
{
    {
        std::lock_guard<std::mutex> lock(messageMutex);
        lastReceivedMessage = receiverStr;
    }

    std::cout << "Server received message: " << receiverStr << std::endl;
}

std::string NetReceiver::getLastReceivedMessage() const
{
    std::lock_guard<std::mutex> lock(messageMutex);
    return lastReceivedMessage;
}

void NetReceiver::runService()
{
    try {
        ioContext = std::make_unique<asio::io_context>();

        const std::size_t workerCount = std::max<std::size_t>(2, std::thread::hardware_concurrency());
        workerPool = std::make_unique<asio::thread_pool>(workerCount);

        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), listenPort);
        acceptor = std::make_unique<asio::ip::tcp::acceptor>(*ioContext);
        acceptor->open(endpoint.protocol());
        acceptor->set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor->bind(endpoint);
        acceptor->listen(asio::socket_base::max_listen_connections);

        acceptLoop();
    } catch (const std::exception &exception) {
        receiveProcess("Server receiver exception: " + std::string(exception.what()));
    }
}

void NetReceiver::acceptLoop()
{
    while (isRunning.load()) {
        auto clientSocket = std::make_shared<asio::ip::tcp::socket>(*ioContext);
        std::error_code errorCode;
        acceptor->accept(*clientSocket, errorCode);

        if (errorCode) {
            if (isRunning.load()) {
                receiveProcess("Server accept failed: " + errorCode.message());
            }
            continue;
        }

        asio::post(*workerPool,
                   [this, clientSocket]() {
                       handleClientSession(clientSocket);
                   });
    }
}

void NetReceiver::handleClientSession(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    std::array<char, 4096> buffer{};
    std::string receiverStr;

    while (isRunning.load()) {
        std::error_code errorCode;
        const std::size_t readSize = clientSocket->read_some(asio::buffer(buffer), errorCode);

        if (errorCode == asio::error::eof) {
            break;
        }

        if (errorCode) {
            receiveProcess("Server session read failed: " + errorCode.message());
            return;
        }

        receiverStr.append(buffer.data(), readSize);
    }

    if (!receiverStr.empty()) {
        receiveProcess(receiverStr);
    }
}
