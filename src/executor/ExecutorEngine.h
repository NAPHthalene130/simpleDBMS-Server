#pragma once

#include <memory>
#include <vector>

#include "StatementExecutor.h"

/**
 * @class ExecutorEngine
 * @brief 执行引擎入口类
 * @details 负责维护语句执行器注册表，并将 SQL 语句分发给对应的执行器处理。
 * @author NAPH130
 */
class ExecutorEngine
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     */
    ExecutorEngine();

    /**
     * @brief 注册语句执行器
     * @author NAPH130
     * @param statementExecutor 语句执行器对象
     */
    void registerExecutor(const std::shared_ptr<StatementExecutor> &statementExecutor);

    /**
     * @brief 检查指定语句类型是否已注册执行器
     * @author NAPH130
     * @param statementType 语句类型
     * @return 是否已注册对应执行器
     */
    bool hasExecutor(StatementType statementType) const;

    /**
     * @brief 执行指定 SQL 语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    ExecutionResult execute(const SQLStatement &statement, ExecutionContext &executionContext);

private:
    /**
     * @brief 查找语句类型对应的执行器
     * @author NAPH130
     * @param statementType 语句类型
     * @return 对应的语句执行器对象
     */
    std::shared_ptr<StatementExecutor> findExecutor(StatementType statementType) const;

private:
    std::vector<std::shared_ptr<StatementExecutor>> statementExecutors;
};
