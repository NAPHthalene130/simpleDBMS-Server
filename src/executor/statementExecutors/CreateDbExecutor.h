#pragma once

#include "models/parser/CreateDbStmt.h"
#include "models/storage/DatabaseBlock.h"
#include "storage/manager/SystemCatalogManager.h"
#include "../StatementExecutor.h"

/**
 * @class CreateDbExecutor
 * @brief 创建数据库语句执行器
 * @details 负责处理 CREATE DATABASE 语句的参数校验、元数据构建与系统目录写入流程。
 * @author NAPH130
 */
class CreateDbExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param systemCatalogManager 系统目录管理器指针
     */
    CreateDbExecutor(Core *core, SystemCatalogManager *systemCatalogManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    ExecutionStatementType getSupportedType() const override;

    /**
     * @brief 执行创建数据库语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    /**
     * @brief 执行创建数据库核心流程
     * @author NAPH130
     * @param createDbStmt 创建数据库语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeCreateDb(const CreateDbStmt *createDbStmt, ExecutionContext *executionContext);

    /**
     * @brief 根据语句内容构建数据库元信息
     * @author NAPH130
     * @param createDbStmt 创建数据库语句对象
     * @return 数据库元信息对象
     */
    DatabaseBlock buildDatabaseBlock(const CreateDbStmt *createDbStmt) const;

private:
    SystemCatalogManager *systemCatalogManager;
};
