#pragma once

#include "models/executor/ExecutionContext.h"
#include "models/executor/ExecutionResult.h"
#include "models/parser/SQLStatement.h"

/**
 * @class StatementExecutor
 * @brief 语句执行器抽象基类
 * @details 为不同 SQL 语句类型提供统一执行接口，便于执行引擎按语句类型进行分发。
 * @author NAPH130
 */
class StatementExecutor
{
public:
    /**
     * @brief 虚析构函数
     * @author NAPH130
     */
    virtual ~StatementExecutor() = default;

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    virtual ExecutionStatementType getSupportedType() const = 0;

    /**
     * @brief 执行指定 SQL 语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    virtual ExecutionResult execute(const SQLStatement &statement, ExecutionContext &executionContext) = 0;
};
