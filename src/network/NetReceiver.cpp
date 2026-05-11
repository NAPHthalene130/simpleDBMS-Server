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
#include "models/network/NetworkExecutionContext.h"
#include "models/network/NetworkTransferData.h"
#include "models/parser/SelectStmt.h"
#include "models/parser/ShowStmt.h"
#include "models/network/SqlData.h"
#include "models/storage/DatabaseBlock.h"
#include "models/storage/TableBlock.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "storage/manager/StorageManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/manager/DatabaseManager.h"
#include "tokenizer/Tokenizer.h"

namespace {
bool isQueryStatementType(ExecutionStatementType statementType)
{
    return statementType == ExecutionStatementType::Select || statementType == ExecutionStatementType::Show;
}

template <std::size_t N>
std::string fixedArrayToStr(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

std::string buildResponseType(const std::string &requestType, ExecutionStatementType stmtType = ExecutionStatementType::Unknown)
{
    if (requestType == NetworkTransferData::SQL_EXEC_REQUEST) {
        if (isQueryStatementType(stmtType)) {
            return NetworkTransferData::SQL_QUERY_RESPONSE;
        }
        return NetworkTransferData::SQL_EXEC_RESPONSE;
    }

    if (requestType == NetworkTransferData::LOGIN_REQUEST) {
        return NetworkTransferData::LOGIN_RESPONSE;
    }

    if (requestType == NetworkTransferData::VERIFY_REQUEST) {
        return NetworkTransferData::VERIFY_RESPONSE;
    }

    if (requestType == NetworkTransferData::USE_DATABASE_REQUEST) {
        return NetworkTransferData::USE_DATABASE_RESPONSE;
    }

    if (requestType == NetworkTransferData::DIRECTORY_REQUEST) {
        return NetworkTransferData::DIRECTORY_RESPONSE;
    }

    return NetworkTransferData::ERROR_RESPONSE;
}

NetworkTransferData buildFailureResponse(const std::string &message,
                                         const NetworkTransferData *requestData = nullptr)
{
    const std::string requestType = requestData != nullptr ? requestData->getType() : "";
    const std::string requestId = requestData != nullptr ? requestData->getId() : "";
    NetworkTransferData responseData(buildResponseType(requestType), requestId);
    responseData.setSuccess(false);
    responseData.setMessage(message);
    if (requestData != nullptr) {
        responseData.setDbName(requestData->getDbName());
    }
    return responseData;
}

NetworkTransferData buildExecutionResponse(const ExecutionResult &executionResult,
                                           const NetworkTransferData &requestData,
                                           ExecutionStatementType statementType)
{
    NetworkTransferData responseData(buildResponseType(requestData.getType(), statementType), requestData.getId());
    responseData.setSuccess(executionResult.getStatus() == ExecutionStatus::Success);
    responseData.setMessage(executionResult.getMessage());
    responseData.setAffectedRows(executionResult.getAffectedRows());
    responseData.setDbName(executionResult.getDbName());
    responseData.setRows(executionResult.getResultSet());
    return responseData;
}

std::vector<std::string> buildQueryColumns(const SQLStatement *statement)
{
    if (statement == nullptr) {
        return {};
    }

    if (statement->getStmtType() == ExecutionStatementType::Select) {
        const SelectStmt *selectStmt = static_cast<const SelectStmt *>(statement);
        if (selectStmt->getSelectAllFields()) {
            return {"name"};
        }
        return selectStmt->getTargetFields();
    }

    if (statement->getStmtType() == ExecutionStatementType::Show) {
        return {"name"};
    }

    return {};
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
 * @details 将所有 SQL 语句统一通过 SQL_EXEC_REQUEST 处理，服务端根据解析后的语句类型自动区分查询/非查询响应。
 * @author NAPH130
 * @param clientSocket 客户端套接字
 * @param networkTransferData 网络层接收到的完整传输对象
 */
void NetReceiver::processMsg(std::shared_ptr<asio::ip::tcp::socket> clientSocket,
                             const NetworkTransferData &networkTransferData)
{
    {
        std::lock_guard<std::mutex> lock(messageMutex);
        lastReceivedMessage = networkTransferData.toJson();
    }

    auto sendResponse = [this, &clientSocket](const NetworkTransferData &responseData) {
        if (core == nullptr || core->getNetworkManager() == nullptr || core->getNetworkManager()->getNetSender() == nullptr) {
            return;
        }

        core->getNetworkManager()->getNetSender()->send(clientSocket, responseData.toJson());
    };

    auto sendFailureResponse = [&](const std::string &message) {
        sendResponse(buildFailureResponse(message, &networkTransferData));
    };

    try {
        if (networkTransferData.getType() == NetworkTransferData::LOGIN_REQUEST) {
            // TODO: 处理登录请求
            sendFailureResponse("LOGIN_REQUEST is not implemented yet.");
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::VERIFY_REQUEST) {
            // TODO: 处理连接验证请求
            sendFailureResponse("VERIFY_REQUEST is not implemented yet.");
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::USE_DATABASE_REQUEST) {
            // TODO: 处理数据库切换请求
            sendFailureResponse("USE_DATABASE_REQUEST is not implemented yet.");
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::SQL_EXEC_REQUEST) {
            if (core == nullptr || core->getParserManager() == nullptr || core->getParserManager()->getParser() == nullptr
                || core->getExecutorManager() == nullptr || core->getExecutorManager()->getExecutorEngine() == nullptr) {
                LogWriter::fatal("network", "NetReceiver", "processMsg", "SQL pipeline is not initialized.");
                sendFailureResponse("SQL pipeline is not initialized.");
                return;
            }

            SqlData sqlData;
            sqlData.setUserID(networkTransferData.getId());
            sqlData.setDbName(networkTransferData.getDbName());
            sqlData.setSql(networkTransferData.getSql());

            if (sqlData.getSql().empty()) {
                LogWriter::warning("network", "NetReceiver", "processMsg", "Rejected empty SQL request.");
                sendFailureResponse("SQL content is empty.");
                return;
            }

            Tokenizer tokenizer(core, sqlData.getSql());
            const std::vector<Token> tokens = tokenizer.tokenize();
            const ParseResult parseResult = core->getParserManager()->getParser()->parse(tokens);
            if (!parseResult.success || parseResult.statement == nullptr) {
                LogWriter::warning("network", "NetReceiver", "processMsg", "SQL parse failed.");
                sendFailureResponse("Parse failed at token " + std::to_string(parseResult.errorTokenIndex)
                                    + ": " + parseResult.errorMessage);
                return;
            }

            const ExecutionStatementType statementType = parseResult.statement->getStmtType();

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

            NetworkTransferData responseData = buildExecutionResponse(executionResult, networkTransferData, statementType);
            if (isQueryStatementType(statementType)) {
                responseData.setColumns(buildQueryColumns(parseResult.statement.get()));
                responseData.setRows(executionResult.getResultSet());
            }
            sendResponse(responseData);
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::DIRECTORY_REQUEST) {
            /**
             * @brief 处理客户端目录结构请求
             * @details 遍历所有数据库→表→字段三层结构，构建 DatabaseNode/TableNode 层级响应。
             * @author NAPH130
             */
            if (core == nullptr || core->getStorageManager() == nullptr) {
                sendFailureResponse("Storage manager is not initialized.");
                return;
            }

            auto *systemCatalogManager = core->getStorageManager()->getSystemCatalogManager();
            auto *databaseManager = core->getStorageManager()->getDatabaseManager();

            if (systemCatalogManager == nullptr || databaseManager == nullptr) {
                sendFailureResponse("Storage components are not initialized.");
                return;
            }

            const auto databaseBlocks = systemCatalogManager->getAllDatabases();

            NetworkTransferData responseData(NetworkTransferData::DIRECTORY_RESPONSE,
                                             networkTransferData.getId());
            responseData.setSuccess(true);
            responseData.setMessage("Directory enumeration succeeded.");

            std::vector<DatabaseNode> databaseNodes;
            databaseNodes.reserve(databaseBlocks.size());

            for (const auto &db : databaseBlocks) {
                const std::string dbName = fixedArrayToStr(db.getName());

                const auto tableBlocks = databaseManager->getAllTablesForDb(dbName);
                std::vector<TableNode> tableNodes;
                tableNodes.reserve(tableBlocks.size());

                for (const auto &tb : tableBlocks) {
                    const std::string tableName = fixedArrayToStr(tb.getName());
                    const auto columns = databaseManager->getTableColumns(dbName, tableName);
                    tableNodes.emplace_back(tableName, columns);
                }

                databaseNodes.emplace_back(dbName, tableNodes);
            }

            responseData.setDatabases(databaseNodes);
            LogWriter::info("network",
                            "NetReceiver",
                            "processMsg",
                            std::string("DIRECTORY_REQUEST returned ")
                                + std::to_string(databaseNodes.size()) + " databases.");
            sendResponse(responseData);
            return;
        }

        sendResponse(buildFailureResponse("Unsupported request type: " + networkTransferData.getType() + "."));
    } catch (const std::exception &exception) {
        LogWriter::error("network",
                         "NetReceiver",
                         "processMsg",
                         std::string("Message processing failed: ") + exception.what());
        sendFailureResponse(exception.what());
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

        try {
            const NetworkTransferData networkTransferData = NetworkTransferData::fromJson(msg);
            processMsg(clientSocket, networkTransferData);
        } catch (const std::exception &exception) {
            {
                std::lock_guard<std::mutex> lock(messageMutex);
                lastReceivedMessage = msg;
            }

            LogWriter::error("network",
                             "NetReceiver",
                             "handleClientSession",
                             std::string("Parse request JSON failed: ") + exception.what());

            if (core != nullptr && core->getNetworkManager() != nullptr
                && core->getNetworkManager()->getNetSender() != nullptr) {
                core->getNetworkManager()->getNetSender()->send(
                    clientSocket,
                    buildFailureResponse("Invalid request JSON.").toJson());
            }
        }
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