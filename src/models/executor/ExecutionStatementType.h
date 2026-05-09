#pragma once

/**
 * @enum ExecutionStatementType
 * @brief 执行器语句类型枚举
 * @details 用于标识 SQL 语句类别，并作为执行器注册与分发的统一类型键。
 * @author NAPH130
 */
enum class ExecutionStatementType
{
    CreateDatabase,
    CreateTable,
    Insert,
    Select,
    UseDatabase,
    Unknown
};
