#pragma once

#include <cstddef>
#include <string>

#include "models/network/NetData.h"

class Core;
class NetworkExecutionContext;
class ExecutionResult;

/**
 * @class SqlPipeline
 * @brief SQL 调用链编排服务
 * @details 负责将网络输入的原始请求串联为 tokenize -> parse -> execute -> response 的完整流程。
 *          本类仅做流程编排与统一错误收敛，不承担词法、语法或执行细节实现。
 * @author YuzhSong
 */
class SqlPipeline
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     * @param core 服务端核心对象指针
     */
    explicit SqlPipeline(Core *core);

    /**
     * @brief 处理网络层输入的原始请求
     * @author YuzhSong
     * @param rawRequest 网络层收到的 JSON 文本
     * @param networkExecutionContext 可选网络会话上下文
     * @return 统一响应对象，type 为 sql_result 或 sql_error
     */
    NetData handleRequest(const std::string &rawRequest,
                          const NetworkExecutionContext *networkExecutionContext = nullptr) const;

private:
    /**
     * @brief 执行 SQL 主流程
     * @author YuzhSong
     * @param sql SQL 文本
     * @param networkExecutionContext 可选网络会话上下文
     * @return 统一响应对象
     */
    NetData handleSql(const std::string &sql, const NetworkExecutionContext *networkExecutionContext) const;

    /**
     * @brief 构造统一错误响应
     * @author YuzhSong
     * @param stage 出错阶段标识
     * @param message 英文错误消息
     * @param tokenIndex 可选错误 token 下标
     * @param hasTokenIndex 是否携带 token 下标
     * @return 错误响应对象
     */
    NetData buildErrorResponse(const std::string &stage,
                               const std::string &message,
                               std::size_t tokenIndex = 0,
                               bool hasTokenIndex = false) const;

    /**
     * @brief 构造执行阶段响应
     * @author YuzhSong
     * @param executionResult 执行结果
     * @return 执行响应对象
     */
    NetData buildExecutionResponse(const ExecutionResult &executionResult) const;

private:
    Core *core;
};
