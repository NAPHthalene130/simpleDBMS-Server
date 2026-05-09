#pragma once

#include "models/parser/UseStmt.h"
#include "storage/manager/SystemCatalogManager.h"
#include "../StatementExecutor.h"

/**
 * @class UseExecutor
 * @brief USE 语句执行器
 * @details 负责处理 USE 语句的数据库存在性校验与执行上下文数据库名称更新。
 * @author YuzhSong
 */
class UseExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param systemCatalogManager 系统目录管理器指针
     */
    UseExecutor(Core *core, SystemCatalogManager *systemCatalogManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author YuzhSong
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行 USE 语句
     * @author YuzhSong
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    /**
     * @brief 执行 USE 核心流程
     * @author YuzhSong
     * @param useStmt USE 语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeUse(const UseStmt *useStmt, ExecutionContext *executionContext);

    SystemCatalogManager *systemCatalogManager;
};
