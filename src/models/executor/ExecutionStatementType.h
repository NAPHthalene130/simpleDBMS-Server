#pragma once

/**
 * @enum ExecutionStatementType
 * @brief 执行器语句类型枚举
 * @details 用于标识 SQL 语句类别，并作为执行器注册与分发的统一类型键。
 * @author NAPH130
 */
enum class ExecutionStatementType
{
    CreateDatabase,  ///< 创建数据库
    DropDatabase,    ///< 删除数据库（预留）
    CreateTable,     ///< 创建表
    DropTable,       ///< 删除表（预留）
    Drop,            ///< 删除操作（泛型，通过 DropTargetType 区分）
    Insert,          ///< 插入记录
    Delete,          ///< 删除记录
    Select,          ///< 查询记录
    Update,          ///< 更新记录
    UseDatabase,     ///< 切换数据库
    Show,            ///< 查看数据库/表
    Use,             ///< 用户相关操作
    UnionSelect,     ///< UNION 查询
    AlterTable,      ///< 修改表结构
    Dcl,             ///< 数据控制语言（GRANT/REVOKE）
    Unknown          ///< 未知类型
};
