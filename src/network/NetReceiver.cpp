#include "NetReceiver.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <system_error>

#include <asio/read.hpp>

#include "Core.h"
#include "NetworkManager.h"
#include "executor/ExecutorEngine.h"
#include "executor/ExecutorManager.h"
#include "models/executor/ExecutionContext.h"
#include "models/executor/ExecutionResult.h"
#include "models/network/NetData.h"
#include "models/network/SqlData.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "tokenizer/Tokenizer.h"

namespace {
bool isSqlRequestType(const std::string &type)
{
    return type == "SQL_USER" || type == "SQL_Client";
}

std::string buildSuccessResponseType(const std::string &requestType)
{
    return requestType == "SQL_USER" ? "SQL_USER_RESULT" : "SQL_Client_RESULT";
}

std::string buildFailureResponseType(const std::string &requestType)
{
    return requestType == "SQL_USER" ? "SQL_USER_ERROR" : "SQL_Client_ERROR";
}

ExecutionResult buildParseFailureResult(const std::string &message, const SqlData &sqlData)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(sqlData.getDbName());
    return executionResult;
}
}

NetReceiver::NetReceiver(Core *core, unsigned short listenPort)
    : core(core),
      listenPort(listenPort),
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

    {
        std::lock_guard<std::mutex> lock(socketMutex);
        for (const std::shared_ptr<asio::ip::tcp::socket> &clientSocket : activeClientSockets) {
            if (clientSocket != nullptr && clientSocket->is_open()) {
                std::error_code errorCode;
                clientSocket->shutdown(asio::ip::tcp::socket::shutdown_both, errorCode);
                clientSocket->close(errorCode);
            }
        }
        activeClientSockets.clear();
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

void NetReceiver::processMsg(std::shared_ptr<asio::ip::tcp::socket> clientSocket, const std::string &msg)
{
    {
        std::lock_guard<std::mutex> lock(messageMutex);
        lastReceivedMessage = msg;
    }

    try {
        const NetData netData = NetData::fromJson(msg);
        if (isSqlRequestType(netData.getType())) {
            if (core == nullptr || core->getTokenizer() == nullptr || core->getParserManager() == nullptr
                || core->getParserManager()->getParser() == nullptr || core->getExecutorManager() == nullptr
                || core->getExecutorManager()->getExecutorEngine() == nullptr) {
                throw std::runtime_error("SQL pipeline is not initialized.");
            }

            const SqlData sqlData = SqlData::fromJson(netData.getContent());

            Tokenizer *tokenizer = core->getTokenizer();
            tokenizer->reset(sqlData.getSql());
            const std::vector<Token> tokens = tokenizer->tokenize();

            const ParseResult parseResult = core->getParserManager()->getParser()->parse(tokens);
            if (!parseResult.success || parseResult.statement == nullptr) {
                ExecutionResult executionResult = buildParseFailureResult(
                    "Parse failed at token " + std::to_string(parseResult.errorTokenIndex)
                        + ": " + parseResult.errorMessage,
                    sqlData);
                if (core->getNetworkManager() != nullptr && core->getNetworkManager()->getNetSender() != nullptr) {
                    core->getNetworkManager()->getNetSender()->send(
                        clientSocket,
                        NetData(buildFailureResponseType(netData.getType()), executionResult.toJson()).toJson());
                }
                return;
            }

            ExecutionContext executionContext;
            if (core->getNetworkManager() != nullptr
                && core->getNetworkManager()->getClientSessionManager() != nullptr) {
                NetworkExecutionContext *networkExecutionContext =
                    core->getNetworkManager()->getClientSessionManager()->findSessionContext(clientSocket.get());
                if (networkExecutionContext != nullptr) {
                    executionContext.setConnectionId(networkExecutionContext->getConnectionId());
                    executionContext.setCurrentUser(networkExecutionContext->getCurrentUser());
                    executionContext.setCurrentDbName(networkExecutionContext->getCurrentDbName());
                }
            }
            executionContext.setCurrentUser(sqlData.getUserID());
            executionContext.setCurrentDbName(sqlData.getDbName());

            ExecutionResult executionResult =
                core->getExecutorManager()->getExecutorEngine()->execute(
                    parseResult.statement.get(),
                    &executionContext);

            if (executionResult.getDbName().empty()) {
                executionResult.setDbName(sqlData.getDbName());
            }

            const std::string responseType =
                executionResult.getStatus() == ExecutionStatus::Success
                    ? buildSuccessResponseType(netData.getType())
                    : buildFailureResponseType(netData.getType());
            const std::string responseContent = executionResult.toJson();

            if (core->getNetworkManager() != nullptr && core->getNetworkManager()->getNetSender() != nullptr) {
                core->getNetworkManager()->getNetSender()->send(
                    clientSocket,
                    NetData(responseType, responseContent).toJson());
            }
        }
        
    } catch (const std::exception &exception) {
        (void)exception;
    }
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
        std::cout << "NetReceiver::runService failed: " << exception.what() << std::endl;
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
                std::cout << "NetReceiver::acceptLoop failed: " << errorCode.message() << std::endl;
            }
            continue;
        }

        addActiveSocket(clientSocket);
        if (core != nullptr && core->getNetworkManager() != nullptr
            && core->getNetworkManager()->getClientSessionManager() != nullptr) {
            core->getNetworkManager()->getClientSessionManager()->addSession(clientSocket.get());
        }

        asio::post(*workerPool,
                   [this, clientSocket]() {
                       handleClientSession(clientSocket);
                   });
    }
}

void NetReceiver::handleClientSession(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    while (isRunning.load()) {
        std::error_code errorCode;
        std::array<unsigned char, 4> lengthHeader{};
        asio::read(*clientSocket, asio::buffer(lengthHeader), errorCode);

        if (errorCode == asio::error::eof) {
            break;
        }

        if (errorCode) {
            std::cout << "NetReceiver::handleClientSession header read failed: "
                      << errorCode.message()
                      << std::endl;
            break;
        }

        const std::uint32_t messageLength = parseLengthHeader(lengthHeader);
        std::string msg(messageLength, '\0');
        asio::read(*clientSocket, asio::buffer(msg.data(), msg.size()), errorCode);

        if (errorCode == asio::error::eof) {
            break;
        }

        if (errorCode) {
            std::cout << "NetReceiver::handleClientSession body read failed: "
                      << errorCode.message()
                      << std::endl;
            break;
        }

        processMsg(clientSocket, msg);
    }

    if (core != nullptr && core->getNetworkManager() != nullptr) {
        if (core->getNetworkManager()->getClientSessionManager() != nullptr) {
            core->getNetworkManager()->getClientSessionManager()->removeSession(clientSocket.get());
        }
        core->getNetworkManager()->disconnected(clientSocket);
    }
    removeActiveSocket(clientSocket);
}

void NetReceiver::addActiveSocket(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    std::lock_guard<std::mutex> lock(socketMutex);
    activeClientSockets.push_back(clientSocket);
}

void NetReceiver::removeActiveSocket(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    std::lock_guard<std::mutex> lock(socketMutex);
    activeClientSockets.erase(std::remove(activeClientSockets.begin(), activeClientSockets.end(), clientSocket),
                              activeClientSockets.end());
}

std::uint32_t NetReceiver::parseLengthHeader(const std::array<unsigned char, 4> &lengthHeader) const
{
    return (static_cast<std::uint32_t>(lengthHeader[0]) << 24U)
           | (static_cast<std::uint32_t>(lengthHeader[1]) << 16U)
           | (static_cast<std::uint32_t>(lengthHeader[2]) << 8U)
           | static_cast<std::uint32_t>(lengthHeader[3]);
}
