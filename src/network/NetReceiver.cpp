#include "NetReceiver.h"

#include <algorithm>
#include <array>
#include <system_error>

#include <asio/read.hpp>

#include "Core.h"
#include "NetworkManager.h"
#include "executor/ExecutorEngine.h"
#include "executor/ExecutorManager.h"
#include "log/LogWriter.h"
#include "models/executor/ExecutionContext.h"
#include "models/executor/ExecutionResult.h"
#include "models/network/NetData.h"
#include "models/network/NetworkExecutionContext.h"
#include "models/network/SqlData.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "tokenizer/Tokenizer.h"

namespace {
bool isSqlRequestType(const std::string &type)
{
    return type == "SQL_USER" || type == "SQL_Client" || type == "sql";
}

std::string buildSuccessResponseType(const std::string &requestType)
{
    if (requestType == "SQL_USER") {
        return "SQL_USER_RESULT";
    }

    if (requestType == "SQL_Client") {
        return "SQL_Client_RESULT";
    }

    return "sql_result";
}

std::string buildFailureResponseType(const std::string &requestType)
{
    if (requestType == "SQL_USER") {
        return "SQL_USER_ERROR";
    }

    if (requestType == "SQL_Client") {
        return "SQL_Client_ERROR";
    }

    return "sql_error";
}

ExecutionResult buildParseFailureResult(const std::string &message, const SqlData &sqlData)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(sqlData.getDbName());
    return executionResult;
}

ExecutionResult buildFailureResult(const std::string &message, const SqlData &sqlData)
{
    ExecutionResult executionResult;
    executionResult.setStatus(ExecutionStatus::Failure);
    executionResult.setMessage(message);
    executionResult.setDbName(sqlData.getDbName());
    return executionResult;
}
} // namespace

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
        LogWriter::warning("network", "NetReceiver", "start", "Receiver is already running.");
        return;
    }

    LogWriter::info("network", "NetReceiver", "start", "Receiver service is starting.");
    serviceThread = std::thread(&NetReceiver::runService, this);
}

void NetReceiver::stop()
{
    if (!isRunning.exchange(false)) {
        LogWriter::warning("network", "NetReceiver", "stop", "Receiver is already stopped.");
        return;
    }

    LogWriter::info("network", "NetReceiver", "stop", "Receiver service is stopping.");

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
    LogWriter::info("network", "NetReceiver", "stop", "Receiver service stopped.");
}

/**
 * @brief 处理客户端完整请求并返回响应
 * @details 网络层仅负责收发与分发，将 JSON 校验、SQL 编排和错误收敛交由 SqlPipeline 处理。
 * @author YuzhSong
 * @param clientSocket 客户端套接字
 * @param msg 网络层接收到的完整消息
 */
void NetReceiver::processMsg(std::shared_ptr<asio::ip::tcp::socket> clientSocket, const std::string &msg)
{
    {
        std::lock_guard<std::mutex> lock(messageMutex);
        lastReceivedMessage = msg;
    }

    try {
        const NetData netData = NetData::fromJson(msg);
        LogWriter::debug("network",
                         "NetReceiver",
                         "processMsg",
                         "Received message type: " + netData.getType());
        if (!isSqlRequestType(netData.getType())) {
            LogWriter::warning("network",
                               "NetReceiver",
                               "processMsg",
                               "Unsupported message type: " + netData.getType() + ".");
            return;
        }

        if (core == nullptr || core->getParserManager() == nullptr || core->getParserManager()->getParser() == nullptr
            || core->getExecutorManager() == nullptr || core->getExecutorManager()->getExecutorEngine() == nullptr) {
            LogWriter::fatal("network", "NetReceiver", "processMsg", "SQL pipeline is not initialized.");
            throw std::runtime_error("SQL pipeline is not initialized.");
        }

        SqlData sqlData;
        if (netData.getType() == "sql") {
            sqlData.setSql(netData.getContent());
        } else {
            sqlData = SqlData::fromJson(netData.getContent());
        }

        Tokenizer tokenizer(core, sqlData.getSql());
        const std::vector<Token> tokens = tokenizer.tokenize();

        const ParseResult parseResult = core->getParserManager()->getParser()->parse(tokens);
        if (!parseResult.success || parseResult.statement == nullptr) {
            ExecutionResult executionResult = buildParseFailureResult(
                "Parse failed at token " + std::to_string(parseResult.errorTokenIndex)
                    + ": " + parseResult.errorMessage,
                sqlData);
            LogWriter::warning("network",
                               "NetReceiver",
                               "processMsg",
                               "SQL parse failed for request type " + netData.getType() + ".");
            if (core->getNetworkManager() != nullptr && core->getNetworkManager()->getNetSender() != nullptr) {
                core->getNetworkManager()->getNetSender()->send(
                    clientSocket,
                    NetData(buildFailureResponseType(netData.getType()), executionResult.toJson()).toJson());
            }
            return;
        }

        ExecutionContext executionContext;
        NetworkExecutionContext *networkExecutionContext = nullptr;
        if (core->getNetworkManager() != nullptr
            && core->getNetworkManager()->getClientSessionManager() != nullptr
            && clientSocket != nullptr) {
            networkExecutionContext =
                core->getNetworkManager()->getClientSessionManager()->findSessionContext(clientSocket.get());
            if (networkExecutionContext != nullptr) {
                executionContext.setConnectionId(networkExecutionContext->getConnectionId());
                executionContext.setCurrentUser(networkExecutionContext->getCurrentUser());
                executionContext.setCurrentDbName(networkExecutionContext->getCurrentDbName());
            }
        }

        if (!sqlData.getUserID().empty()) {
            executionContext.setCurrentUser(sqlData.getUserID());
        }
        if (!sqlData.getDbName().empty()) {
            executionContext.setCurrentDbName(sqlData.getDbName());
        }

        ExecutionResult executionResult =
            core->getExecutorManager()->getExecutorEngine()->execute(parseResult.statement.get(), &executionContext);

        if (executionResult.getStatus() == ExecutionStatus::Success
            && networkExecutionContext != nullptr
            && !executionResult.getDbName().empty()) {
            networkExecutionContext->setCurrentDbName(executionResult.getDbName());
        }

        if (executionResult.getDbName().empty()) {
            executionResult.setDbName(executionContext.getCurrentDbName());
        }

        const std::string responseType =
            executionResult.getStatus() == ExecutionStatus::Success
                ? buildSuccessResponseType(netData.getType())
                : buildFailureResponseType(netData.getType());
        const std::string responseContent = executionResult.toJson();
        LogWriter::info("network",
                        "NetReceiver",
                        "processMsg",
                        "SQL request processed with response type " + responseType + ".");

        if (core->getNetworkManager() != nullptr && core->getNetworkManager()->getNetSender() != nullptr) {
            core->getNetworkManager()->getNetSender()->send(
                clientSocket,
                NetData(responseType, responseContent).toJson());
        }
    } catch (const std::exception &exception) {
        LogWriter::error("network",
                         "NetReceiver",
                         "processMsg",
                         std::string("Message processing failed: ") + exception.what());

        SqlData sqlData;
        std::string responseType = "sql_error";
        try {
            const NetData netData = NetData::fromJson(msg);
            responseType = buildFailureResponseType(netData.getType());
            if (isSqlRequestType(netData.getType()) && netData.getType() != "sql") {
                sqlData = SqlData::fromJson(netData.getContent());
            }
        } catch (const std::exception &) {
        }

        if (core != nullptr && core->getNetworkManager() != nullptr && core->getNetworkManager()->getNetSender() != nullptr) {
            const ExecutionResult executionResult = buildFailureResult(exception.what(), sqlData);
            core->getNetworkManager()->getNetSender()->send(
                clientSocket,
                NetData(responseType, executionResult.toJson()).toJson());
        }
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

        LogWriter::info("network", "NetReceiver", "runService", "Receiver service is listening for connections.");
        acceptLoop();
    } catch (const std::exception &exception) {
        LogWriter::error("network",
                         "NetReceiver",
                         "runService",
                         std::string("Receiver service failed: ") + exception.what());
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
                LogWriter::error("network",
                                 "NetReceiver",
                                 "acceptLoop",
                                 "Accept client failed: " + errorCode.message());
            }
            continue;
        }

        LogWriter::info("network", "NetReceiver", "acceptLoop", "Accepted a client connection.");
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
            LogWriter::error("network",
                             "NetReceiver",
                             "handleClientSession",
                             "Read message header failed: " + errorCode.message());
            break;
        }

        const std::uint32_t messageLength = parseLengthHeader(lengthHeader);
        std::string msg(messageLength, '\0');
        asio::read(*clientSocket, asio::buffer(msg.data(), msg.size()), errorCode);

        if (errorCode == asio::error::eof) {
            break;
        }

        if (errorCode) {
            LogWriter::error("network",
                             "NetReceiver",
                             "handleClientSession",
                             "Read message body failed: " + errorCode.message());
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
    LogWriter::info("network", "NetReceiver", "handleClientSession", "Client session finished.");
}

void NetReceiver::addActiveSocket(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    std::lock_guard<std::mutex> lock(socketMutex);
    activeClientSockets.push_back(clientSocket);
    LogWriter::debug("network",
                     "NetReceiver",
                     "addActiveSocket",
                     "Active client count is now " + std::to_string(activeClientSockets.size()) + ".");
}

void NetReceiver::removeActiveSocket(std::shared_ptr<asio::ip::tcp::socket> clientSocket)
{
    std::lock_guard<std::mutex> lock(socketMutex);
    activeClientSockets.erase(std::remove(activeClientSockets.begin(), activeClientSockets.end(), clientSocket),
                              activeClientSockets.end());
    LogWriter::debug("network",
                     "NetReceiver",
                     "removeActiveSocket",
                     "Active client count is now " + std::to_string(activeClientSockets.size()) + ".");
}

std::uint32_t NetReceiver::parseLengthHeader(const std::array<unsigned char, 4> &lengthHeader) const
{
    return (static_cast<std::uint32_t>(lengthHeader[0]) << 24U)
           | (static_cast<std::uint32_t>(lengthHeader[1]) << 16U)
           | (static_cast<std::uint32_t>(lengthHeader[2]) << 8U)
           | static_cast<std::uint32_t>(lengthHeader[3]);
}
