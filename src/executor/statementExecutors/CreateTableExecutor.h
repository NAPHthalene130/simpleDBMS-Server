#pragma once

#include <vector>

#include "models/parser/CreateTableStmt.h"
#include "models/storage/FieldBlock.h"
#include "models/storage/TableBlock.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/TableDefManager.h"
#include "../StatementExecutor.h"

/**
 * @class CreateTableExecutor
 * @brief 创建数据表语句执行器
 * @details 负责处理 CREATE TABLE 语句的表结构转换、字段定义整理与表元数据写入流程。
 * @author NAPH130
 */
class CreateTableExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param databaseManager 数据库管理器
     * @param tableDefManager 表定义管理器
     */
    CreateTableExecutor(DatabaseManager &databaseManager, TableDefManager &tableDefManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    StatementType getSupportedType() const override;

    /**
     * @brief 执行创建数据表语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    ExecutionResult execute(const SQLStatement &statement, ExecutionContext &executionContext) override;

private:
    /**
     * @brief 执行创建数据表核心流程
     * @author NAPH130
     * @param createTableStmt 创建数据表语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeCreateTable(const CreateTableStmt &createTableStmt, ExecutionContext &executionContext);

    /**
     * @brief 根据语句内容构建表元信息
     * @author NAPH130
     * @param createTableStmt 创建数据表语句对象
     * @return 表元信息对象
     */
    TableBlock buildTableBlock(const CreateTableStmt &createTableStmt) const;

    /**
     * @brief 提取并整理字段定义列表
     * @author NAPH130
     * @param createTableStmt 创建数据表语句对象
     * @return 字段定义列表
     */
    std::vector<FieldBlock> buildFieldBlocks(const CreateTableStmt &createTableStmt) const;

private:
    DatabaseManager &databaseManager;
    TableDefManager &tableDefManager;
};
