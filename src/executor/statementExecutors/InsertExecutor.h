#pragma once

#include "models/parser/InsertStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "storage/manager/TableDefManager.h"
#include "../StatementExecutor.h"

/**
 * @class InsertExecutor
 * @brief 插入语句执行器
 * @details 负责处理 INSERT INTO 语句的字段映射校验、记录写入准备与结果封装流程。
 * @author NAPH130
 */
class InsertExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param databaseManager 数据库管理器
     * @param tableDefManager 表定义管理器
     */
    InsertExecutor(DatabaseManager &databaseManager, TableDefManager &tableDefManager);

    /**
     * @brief 获取当前执行器支持的语句类型
     * @author NAPH130
     * @return 支持的语句类型
     */
    StatementType getSupportedType() const override;

    /**
     * @brief 执行插入语句
     * @author NAPH130
     * @param statement 待执行的 SQL 语句对象
     * @param executionContext 当前执行上下文
     * @return 统一的执行结果对象
     */
    ExecutionResult execute(const SQLStatement &statement, ExecutionContext &executionContext) override;

private:
    /**
     * @brief 执行插入语句核心流程
     * @author NAPH130
     * @param insertStmt 插入语句对象
     * @param executionContext 当前执行上下文
     * @return 执行结果对象
     */
    ExecutionResult executeInsert(const InsertStmt &insertStmt, ExecutionContext &executionContext);

    /**
     * @brief 校验插入语句字段和值是否匹配
     * @author NAPH130
     * @param insertStmt 插入语句对象
     * @return 是否通过基础校验
     */
    bool validateInsertStmt(const InsertStmt &insertStmt) const;

private:
    DatabaseManager &databaseManager;
    TableDefManager &tableDefManager;
};
