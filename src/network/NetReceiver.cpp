#include "NetReceiver.h"

#include <algorithm>
#include <array>
#include <system_error>

#include <asio/read.hpp>

#include "Core.h"
#include "NetworkManager.h"
#include "binder/Binder.h"
#include "binder/BinderManager.h"
#include "executor/ExecutorEngine.h"
#include "executor/ExecutorManager.h"
#include "log/LogWriter.h"
#include "models/executor/ExecutionContext.h"
#include "models/executor/ExecutionResult.h"
#include "models/network/NetworkExecutionContext.h"
#include "models/network/NetworkTransferData.h"
#include "models/parser/SelectStmt.h"
#include "models/parser/ShowStmt.h"
#include "models/parser/UnionStmt.h"
#include "models/network/SqlData.h"
#include "models/storage/DatabaseBlock.h"
#include "models/storage/TableBlock.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "plan/Planner.h"
#include "plan/PlanManager.h"
#include "plan/PlanNode.h"
#include "plan/PlanExecutor.h"
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
        if (!selectStmt->getSelectAllFields()) {
            return selectStmt->getTargetFields();
        }
        return {};
    }

    if (statement->getStmtType() == ExecutionStatementType::Show) {
        return {"name"};
    }

    return {};
}

/**
 * @brief 将 token 序列按分号分割为多条独立的 token 子序列
 * @details 以 Symbol(";") 为分隔符切割 token 序列，
 *          自动过滤空语句和末尾的 EndOfFile token。
 *          此方法基于 Tokenizer 已正确解析字符串字面量和注释，
 *          因此分号在字符串内部时不会误分割。
 * @author NAPH130
 * @param tokens 完整的 token 序列
 * @return 分割后的 token 子序列列表
 */
static std::vector<std::vector<Token>> splitTokensBySemicolon(const std::vector<Token> &tokens)
{
    std::vector<std::vector<Token>> statementTokensList;
    std::vector<Token> currentStatement;

    for (const auto &token : tokens) {
        if (token.getType() == SqlTokenType::EndOfFile) {
            break;
        }

        if (token.getType() == SqlTokenType::Symbol && token.getValue() == ";") {
            if (!currentStatement.empty()) {
                statementTokensList.push_back(std::move(currentStatement));
                currentStatement.clear();
            }
        } else {
            currentStatement.push_back(token);
        }
    }

    if (!currentStatement.empty()) {
        statementTokensList.push_back(std::move(currentStatement));
    }

    return statementTokensList;
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
            /**
             * @brief 处理登录请求，校验 system.user 表中 id 与密码
             * @author NAPH130
             * @details 从 system 库的 user 表中查询 id 对应的记录并比对密码。
             *          匹配成功返回 LOGIN_RESPONSE 并更新会话上下文中的用户名。
             */
            if (core == nullptr || core->getStorageManager() == nullptr) {
                sendFailureResponse("Storage manager is not initialized.");
                return;
            }

            auto *databaseManager = core->getStorageManager()->getDatabaseManager();
            if (databaseManager == nullptr) {
                sendFailureResponse("Database manager is not initialized.");
                return;
            }

            const std::string userId = networkTransferData.getId();
            const std::string password = networkTransferData.getPassword();

            if (userId.empty()) {
                sendFailureResponse("User ID is empty.");
                return;
            }

            // 查询 system.user 表
            // 作者：NAPH130
            const std::string systemDbName = "system";
            const std::string userTableName = "user";
            const auto tables = databaseManager->getAllTablesForDb(systemDbName);
            const bool userTableExists = std::any_of(tables.begin(), tables.end(),
                                                     [&](const TableBlock &tb) {
                                                         const std::string name = fixedArrayToStr(tb.getName());
                                                         return name == userTableName;
                                                     });

            if (!userTableExists) {
                sendFailureResponse("System user table does not exist.");
                return;
            }

            try {
                const std::vector<storage::Table::WhereCondition> conditions = {
                    {"id", storage::Table::CompareOp::EQ, userId}
                };
                const auto rows = databaseManager->selectRows(systemDbName, userTableName, {}, conditions);
                bool loginSuccess = false;
                for (const auto &row : rows) {
                    if (row.values.size() >= 2 && row.values[1] == password) {
                        loginSuccess = true;
                        break;
                    }
                }

                NetworkTransferData responseData(NetworkTransferData::LOGIN_RESPONSE,
                                                  networkTransferData.getId());
                responseData.setSuccess(loginSuccess);
                if (loginSuccess) {
                    responseData.setMessage("Login succeeded.");
                    NetworkExecutionContext *sessionCtx = nullptr;
                    if (core->getNetworkManager() != nullptr
                        && core->getNetworkManager()->getClientSessionManager() != nullptr
                        && clientSocket != nullptr) {
                        sessionCtx = core->getNetworkManager()->getClientSessionManager()
                                         ->findSessionContext(clientSocket.get());
                        if (sessionCtx != nullptr) {
                            sessionCtx->setCurrentUser(userId);
                            sessionCtx->setIsAuthorized(true);
                        }
                    }
                    LogWriter::info("network", "NetReceiver", "processMsg",
                                    std::string("User logged in: ") + userId);
                } else {
                    responseData.setMessage("Login failed: invalid user ID or password.");
                    LogWriter::warning("network", "NetReceiver", "processMsg",
                                       std::string("Login failed for user: ") + userId);
                }
                sendResponse(responseData);
            } catch (const std::exception &exception) {
                LogWriter::error("network", "NetReceiver", "processMsg",
                                 std::string("Login query failed: ") + exception.what());
                sendFailureResponse("Login query failed: " + std::string(exception.what()));
            }
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::VERIFY_REQUEST) {
            /**
             * @brief 处理连接验证请求，校验 system.user 表中 id 与密码
             * @author NAPH130
             * @details 与登录类似但不创建会话授权，仅返回验证结果。
             */
            if (core == nullptr || core->getStorageManager() == nullptr) {
                sendFailureResponse("Storage manager is not initialized.");
                return;
            }

            auto *databaseManager = core->getStorageManager()->getDatabaseManager();
            if (databaseManager == nullptr) {
                sendFailureResponse("Database manager is not initialized.");
                return;
            }

            const std::string userId = networkTransferData.getId();
            const std::string password = networkTransferData.getPassword();

            if (userId.empty()) {
                sendFailureResponse("User ID is empty.");
                return;
            }

            const std::string systemDbName = "system";
            const std::string userTableName = "user";

            try {
                const std::vector<storage::Table::WhereCondition> conditions = {
                    {"id", storage::Table::CompareOp::EQ, userId}
                };
                const auto rows = databaseManager->selectRows(systemDbName, userTableName, {}, conditions);
                bool verifySuccess = false;
                for (const auto &row : rows) {
                    if (row.values.size() >= 2 && row.values[1] == password) {
                        verifySuccess = true;
                        break;
                    }
                }

                NetworkTransferData responseData(NetworkTransferData::VERIFY_RESPONSE,
                                                  networkTransferData.getId());
                responseData.setSuccess(verifySuccess);
                responseData.setMessage(verifySuccess ? "Connection verified." : "Verification failed: invalid credentials.");
                sendResponse(responseData);
            } catch (const std::exception &exception) {
                LogWriter::error("network", "NetReceiver", "processMsg",
                                 std::string("Verify query failed: ") + exception.what());
                sendFailureResponse("Verify query failed: " + std::string(exception.what()));
            }
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::USE_DATABASE_REQUEST) {
            // TODO: 处理数据库切换请求
            sendFailureResponse("USE_DATABASE_REQUEST is not implemented yet.");
            return;
        }

        if (networkTransferData.getType() == NetworkTransferData::SQL_EXEC_REQUEST) {
            /**
             * @brief 处理 SQL 执行请求，支持多语句（按分号分割）
             * @details 将输入 SQL 按分号分割为多条独立语句，逐条执行并逐个响应。
             *          执行上下文在语句之间传递（如 USE DATABASE 影响后续语句）。
             * @author NAPH130
             */
            if (core == nullptr || core->getParserManager() == nullptr || core->getParserManager()->getParser() == nullptr
                || core->getExecutorManager() == nullptr || core->getExecutorManager()->getExecutorEngine() == nullptr) {
                LogWriter::fatal("network", "NetReceiver", "processMsg", "SQL pipeline is not initialized.");
                sendFailureResponse("SQL pipeline is not initialized.");
                return;
            }

            const std::string rawSql = networkTransferData.getSql();
            if (rawSql.empty()) {
                LogWriter::warning("network", "NetReceiver", "processMsg", "Rejected empty SQL request.");
                sendFailureResponse("SQL content is empty.");
                return;
            }

            // 1. 对整个 SQL 文本进行词法分析
            // 作者：NAPH130
            Tokenizer tokenizer(core, rawSql);
            const std::vector<Token> allTokens = tokenizer.tokenize();

            // 2. 按分号分割为多条语句的 token 子序列
            // 作者：NAPH130
            const std::vector<std::vector<Token>> statementTokensList = splitTokensBySemicolon(allTokens);

            if (statementTokensList.empty()) {
                LogWriter::warning("network", "NetReceiver", "processMsg", "No SQL statements found.");
                sendFailureResponse("No SQL statements found.");
                return;
            }

            // 3. 构建执行上下文（跨语句共享）
            // 作者：NAPH130
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

            if (!networkTransferData.getId().empty()) {
                executionContext.setCurrentUser(networkTransferData.getId());
            }
            if (!networkTransferData.getDbName().empty()) {
                executionContext.setCurrentDbName(networkTransferData.getDbName());
            }

            // 4. 逐条执行每一条语句
            // 作者：NAPH130
            for (std::size_t stmtIdx = 0; stmtIdx < statementTokensList.size(); ++stmtIdx) {
                const std::vector<Token> &stmtTokens = statementTokensList[stmtIdx];
                const std::size_t statementNumber = stmtIdx + 1;

                try {
                    // 4a. 语法分析
                    // 作者：NAPH130
                    const ParseResult parseResult = core->getParserManager()->getParser()->parse(stmtTokens);
                    if (!parseResult.success || parseResult.statement == nullptr) {
                        LogWriter::warning("network", "NetReceiver", "processMsg",
                                           "Statement " + std::to_string(statementNumber) + " parse failed.");
                        NetworkTransferData errorResponse = buildFailureResponse(
                            "Statement " + std::to_string(statementNumber)
                            + " parse failed at token " + std::to_string(parseResult.errorTokenIndex)
                            + ": " + parseResult.errorMessage,
                            &networkTransferData);
                        sendResponse(errorResponse);
                        continue;
                    }

                    const ExecutionStatementType statementType = parseResult.statement->getStmtType();
                    const std::string currentDbName = executionContext.getCurrentDbName();

                    // 4b. 对 SELECT 语句优先使用 Binder + Plan 管道（支持 JOIN 和聚合）
                    // 作者：NAPH130
                    bool usedBindPlan = false;
                    if (statementType == ExecutionStatementType::Select && !currentDbName.empty()) {
                        const SelectStmt *selectStmt = static_cast<const SelectStmt *>(parseResult.statement.get());
                        if (core->getBinderManager() != nullptr) {
                            const BindResult bindResult =
                                core->getBinderManager()->getBinder()->bindSelect(selectStmt, currentDbName);

                            if (bindResult.success && core->getPlanManager() != nullptr) {
                                auto planRoot = core->getPlanManager()->getPlanner()->planSelect(bindResult, currentDbName);

                                if (planRoot != nullptr) {
                                    PlanExecutor planExecutor(core);
                                    ExecutionResult planExecResult = planExecutor.execute(
                                        planRoot, currentDbName, selectStmt, &executionContext);

                                    if (networkExecutionContext != nullptr && !planExecResult.getDbName().empty()) {
                                        networkExecutionContext->setCurrentDbName(planExecResult.getDbName());
                                    }
                                    if (!planExecResult.getDbName().empty()) {
                                        executionContext.setCurrentDbName(planExecResult.getDbName());
                                    }

                                    if (planExecResult.getDbName().empty()) {
                                        planExecResult.setDbName(executionContext.getCurrentDbName());
                                    }

                                    NetworkTransferData responseData = buildExecutionResponse(
                                        planExecResult, networkTransferData, statementType);
                                    responseData.setColumns(planExecResult.getColumns());
                                    responseData.setRows(planExecResult.getResultSet());
                                    sendResponse(responseData);
                                    usedBindPlan = true;
                                }
                            }
                        }
                    }

                    // 4b2. UNION 查询处理
                    // 作者：NAPH130
                    if (statementType == ExecutionStatementType::UnionSelect) {
                        const UnionStmt *unionStmt = static_cast<const UnionStmt *>(parseResult.statement.get());
                        // 递归执行左右两侧子查询
                        // 作者：NAPH130
                        auto execSingleSelect = [&](const std::shared_ptr<SQLStatement> &subStmt) -> ExecutionResult {
                            if (subStmt == nullptr) {
                                ExecutionResult r;
                                r.setStatus(ExecutionStatus::Failure);
                                r.setMessage("union sub-statement is null");
                                return r;
                            }
                            // 复用 Binder+Plan 管道
                            // 作者：NAPH130
                            if (subStmt->getStmtType() == ExecutionStatementType::Select) {
                                const SelectStmt *sel = static_cast<const SelectStmt *>(subStmt.get());
                                const BindResult bindResult =
                                    core->getBinderManager()->getBinder()->bindSelect(sel, currentDbName);
                                if (bindResult.success) {
                                    auto planRoot = core->getPlanManager()->getPlanner()->planSelect(
                                        bindResult, currentDbName);
                                    if (planRoot) {
                                        PlanExecutor pe(core);
                                        return pe.execute(planRoot, currentDbName, sel, &executionContext);
                                    }
                                }
                            }
                            // 回退到旧执行器
                            // 作者：NAPH130
                            return core->getExecutorManager()->getExecutorEngine()->execute(
                                subStmt.get(), &executionContext);
                        };

                        ExecutionResult leftResult = execSingleSelect(unionStmt->getLeftStmt());
                        ExecutionResult rightResult = execSingleSelect(unionStmt->getRightStmt());

                        // 合并结果
                        // 作者：NAPH130
                        std::vector<std::string> unionColumns;
                        std::vector<std::vector<std::string>> unionRows;

                        if (leftResult.getStatus() == ExecutionStatus::Success
                            && rightResult.getStatus() == ExecutionStatus::Success) {
                            unionColumns = leftResult.getColumns();
                            // 左侧结果
                            // 作者：NAPH130
                            for (const auto &row : leftResult.getResultSet()) {
                                unionRows.push_back(row);
                            }
                            // 右侧结果（UNION 去重，UNION ALL 不去重）
                            // 作者：NAPH130
                            for (const auto &row : rightResult.getResultSet()) {
                                if (unionStmt->isUnionAll()) {
                                    unionRows.push_back(row);
                                } else {
                                    // 简单去重：检查是否已存在
                                    // 作者：NAPH130
                                    bool duplicate = false;
                                    for (const auto &existing : unionRows) {
                                        if (existing == row) {
                                            duplicate = true;
                                            break;
                                        }
                                    }
                                    if (!duplicate) {
                                        unionRows.push_back(row);
                                    }
                                }
                            }
                            ExecutionResult unionResult;
                            unionResult.setStatus(ExecutionStatus::Success);
                            unionResult.setMessage("UNION succeeded.");
                            unionResult.setColumns(unionColumns);
                            unionResult.setResultSet(unionRows);
                            unionResult.setAffectedRows(static_cast<std::int32_t>(unionRows.size()));
                            unionResult.setDbName(currentDbName);

                            NetworkTransferData responseData =
                                buildExecutionResponse(unionResult, networkTransferData, ExecutionStatementType::Select);
                            responseData.setColumns(unionColumns);
                            responseData.setRows(unionRows);
                            sendResponse(responseData);
                        } else {
                            NetworkTransferData errorResponse = buildFailureResponse(
                                "UNION failed: " + leftResult.getMessage() + " / " + rightResult.getMessage(),
                                &networkTransferData);
                            sendResponse(errorResponse);
                        }
                        continue;
                    }

                    if (usedBindPlan) {
                        continue;
                    }

                    // 4c. 普通执行路径（非 SELECT 或 binder/plan 不可用时）
                    // 作者：NAPH130
                    ExecutionResult executionResult =
                        core->getExecutorManager()->getExecutorEngine()->execute(
                            parseResult.statement.get(), &executionContext);

                    if (executionResult.getStatus() == ExecutionStatus::Success
                        && networkExecutionContext != nullptr
                        && !executionResult.getDbName().empty()) {
                        networkExecutionContext->setCurrentDbName(executionResult.getDbName());
                    }
                    if (!executionResult.getDbName().empty()) {
                        executionContext.setCurrentDbName(executionResult.getDbName());
                    }

                    if (executionResult.getDbName().empty()) {
                        executionResult.setDbName(executionContext.getCurrentDbName());
                    }

                    NetworkTransferData responseData = buildExecutionResponse(
                        executionResult, networkTransferData, statementType);
                    if (isQueryStatementType(statementType)) {
                        std::vector<std::string> resultColumns = executionResult.getColumns();
                        if (resultColumns.empty()) {
                            resultColumns = buildQueryColumns(parseResult.statement.get());
                        }
                        responseData.setColumns(resultColumns);
                        responseData.setRows(executionResult.getResultSet());
                    }
                    sendResponse(responseData);

                } catch (const std::exception &exception) {
                    LogWriter::error("network", "NetReceiver", "processMsg",
                                     "Statement " + std::to_string(statementNumber)
                                     + " execution failed: " + std::string(exception.what()));
                    NetworkTransferData errorResponse = buildFailureResponse(
                        "Statement " + std::to_string(statementNumber)
                        + " execution failed: " + std::string(exception.what()),
                        &networkTransferData);
                    sendResponse(errorResponse);
                }
            }
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