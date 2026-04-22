#include "core/SqlPipeline.h"

#include <exception>

#include <nlohmann/json.hpp>

#include "Core.h"
#include "executor/ExecutorEngine.h"
#include "executor/ExecutorManager.h"
#include "models/executor/ExecutionContext.h"
#include "models/executor/ExecutionResult.h"
#include "models/network/NetworkExecutionContext.h"
#include "models/parser/ParseResult.h"
#include "parser/Parser.h"
#include "parser/ParserManager.h"
#include "tokenizer/Tokenizer.h"

/**
 * @brief 构造函数
 * @author YuzhSong
 * @param core 服务端核心对象指针
 */
SqlPipeline::SqlPipeline(Core *core)
    : core(core)
{
}

/**
 * @brief 处理网络层输入的原始请求
 * @author YuzhSong
 * @param rawRequest 网络层收到的 JSON 文本
 * @param networkExecutionContext 可选网络会话上下文
 * @return 统一响应对象，type 为 sql_result 或 sql_error
 */
NetData SqlPipeline::handleRequest(const std::string &rawRequest,
                                   const NetworkExecutionContext *networkExecutionContext) const
{
    try {
        const NetData requestData = NetData::fromJson(rawRequest);
        if (requestData.getType() != "sql") {
            return buildErrorResponse("request", "Unsupported request type. Only 'sql' is accepted.");
        }

        if (requestData.getContent().empty()) {
            return buildErrorResponse("request", "SQL content is empty.");
        }

        return handleSql(requestData.getContent(), networkExecutionContext);
    } catch (const std::exception &) {
        return buildErrorResponse("request", "Invalid JSON request.");
    }
}

/**
 * @brief 执行 SQL 主流程
 * @author YuzhSong
 * @param sql SQL 文本
 * @param networkExecutionContext 可选网络会话上下文
 * @return 统一响应对象
 */
NetData SqlPipeline::handleSql(const std::string &sql, const NetworkExecutionContext *networkExecutionContext) const
{
    if (core == nullptr) {
        return buildErrorResponse("pipeline", "Core is not initialized.");
    }
    if (core->getParserManager() == nullptr || core->getParserManager()->getParser() == nullptr) {
        return buildErrorResponse("pipeline", "Parser manager is not available.");
    }
    if (core->getExecutorManager() == nullptr || core->getExecutorManager()->getExecutorEngine() == nullptr) {
        return buildErrorResponse("pipeline", "Executor engine is not available.");
    }

    Tokenizer tokenizer(core, sql);
    const std::vector<Token> tokens = tokenizer.tokenize();

    const ParseResult parseResult = core->getParserManager()->getParser()->parse(tokens);
    if (!parseResult.success || parseResult.statement == nullptr) {
        return buildErrorResponse("parser", parseResult.errorMessage, parseResult.errorTokenIndex, true);
    }

    ExecutionContext executionContext;
    if (networkExecutionContext != nullptr) {
        executionContext.setConnectionId(networkExecutionContext->getConnectionId());
        executionContext.setCurrentUser(networkExecutionContext->getCurrentUser());
        executionContext.setCurrentDbName(networkExecutionContext->getCurrentDbName());
    }

    const ExecutionResult executionResult =
        core->getExecutorManager()->getExecutorEngine()->execute(parseResult.statement.get(), &executionContext);
    return buildExecutionResponse(executionResult);
}

/**
 * @brief 构造统一错误响应
 * @author YuzhSong
 * @param stage 出错阶段标识
 * @param message 英文错误消息
 * @param tokenIndex 可选错误 token 下标
 * @param hasTokenIndex 是否携带 token 下标
 * @return 错误响应对象
 */
NetData SqlPipeline::buildErrorResponse(const std::string &stage,
                                        const std::string &message,
                                        const std::size_t tokenIndex,
                                        const bool hasTokenIndex) const
{
    nlohmann::json responseObject;
    responseObject["success"] = false;
    responseObject["stage"] = stage;
    responseObject["message"] = message;
    if (hasTokenIndex) {
        responseObject["errorTokenIndex"] = tokenIndex;
    }

    return NetData("sql_error", responseObject.dump());
}

/**
 * @brief 构造执行阶段响应
 * @author YuzhSong
 * @param executionResult 执行结果
 * @return 执行响应对象
 */
NetData SqlPipeline::buildExecutionResponse(const ExecutionResult &executionResult) const
{
    const bool success = executionResult.getStatus() == ExecutionStatus::Success;

    nlohmann::json responseObject;
    responseObject["success"] = success;
    responseObject["stage"] = "executor";
    responseObject["message"] = executionResult.getMessage();
    responseObject["affectedRows"] = executionResult.getAffectedRows();
    responseObject["resultSet"] = executionResult.getResultSet();

    return NetData(success ? "sql_result" : "sql_error", responseObject.dump());
}
