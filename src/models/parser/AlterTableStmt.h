#pragma once

#include <cstdint>
#include <string>

#include "SQLStatement.h"

/**
 * @enum AlterTableTargetType
 * @brief ALTER TABLE 操作目标类型枚举
 * @details 用于区分不同的 ALTER TABLE 子操作。
 * @author NAPH130
 */
enum class AlterTableTargetType
{
    AddColumn,        ///< 添加列
    DropColumn,       ///< 删除列
    RenameColumn,     ///< 重命名列
    AlterColumnType   ///< 修改列类型
};

/**
 * @class AlterTableStmt
 * @brief 修改表结构语句数据类
 * @details 封装 ALTER TABLE 语句解析后的表名、操作类型及操作参数。
 * @author NAPH130
 */
class AlterTableStmt : public SQLStatement
{
public:
    AlterTableStmt();

    const std::string &getTableName() const;
    void setTableName(const std::string &tableName);

    AlterTableTargetType getTargetType() const;
    void setTargetType(AlterTableTargetType targetType);

    const std::string &getColumnName() const;
    void setColumnName(const std::string &columnName);

    const std::string &getNewColumnName() const;
    void setNewColumnName(const std::string &newColumnName);

    const std::string &getColumnType() const;
    void setColumnType(const std::string &columnType);

    std::uint16_t getVarcharLen() const;
    void setVarcharLen(std::uint16_t varcharLen);

    const std::string &getDefaultValue() const;
    void setDefaultValue(const std::string &defaultValue);

private:
    std::string tableName;
    AlterTableTargetType targetType;
    std::string columnName;
    std::string newColumnName;
    std::string columnType;
    std::uint16_t varcharLen = 0;
    std::string defaultValue;
};
