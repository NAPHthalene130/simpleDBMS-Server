#pragma once

#include "models/parser/ShowStmt.h"
#include "../StatementExecutor.h"

/**
 * @class ShowExecutor
 * @brief SHOW 语句执行器
 * @details 负责处理 SHOW 语句并返回最小可运行结果集，当前以 stub 结果为主。
 * @author YuzhSong
 */
class ShowExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author YuzhSong
     * @param core 服务端核心对象指针
     */
    explicit ShowExecutor(Core *core);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author YuzhSong
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行 SHOW 语句
     * @author YuzhSong
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;
};
