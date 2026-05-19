#pragma once

#include <string>

#include "models/parser/AlterTableStmt.h"
#include "storage/manager/DatabaseManager.h"
#include "../StatementExecutor.h"

/**
 * @class AlterTableExecutor
 * @brief ALTER TABLE 语句执行器
 * @details 负责处理 ALTER TABLE 中的 ADD COLUMN / DROP COLUMN / RENAME COLUMN / ALTER COLUMN TYPE
 *          操作，通过 DatabaseManager 下层的存储接口完成列修改。
 * @author NAPH130
 */
class AlterTableExecutor : public StatementExecutor
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     * @param databaseManager 数据库管理器指针
     */
    AlterTableExecutor(Core *core, DatabaseManager *databaseManager);

    ExecutionStatementType getSupportedType() const override;

    ExecutionResult execute(const SQLStatement *statement, ExecutionContext *executionContext) override;

private:
    /**
     * @brief 将列类型字符串映射为 storage::DataType
     * @author NAPH130
     * @param typeStr 类型字符串（INT/FLOAT/VARCHAR/DOUBLE/DECIMAL/DATE/TEXT 等）
     * @return 对应的存储数据类型
     */
    storage::DataType mapColumnType(const std::string &typeStr) const;

    DatabaseManager *databaseManager;
};
