#pragma once

#include <cstdint>
#include <string>

#include "../storage/DateTime.h"
#include <nlohmann/json.hpp>

/**
 * @enum DbLogOperationType
 * @brief 数据库日志操作类型枚举
 * @details 用于标识触发日志记录的操作类别，
 *          覆盖 DDL 与 DML 两大类变更操作。
 * @author NAPH130
 */
enum class DbLogOperationType
{
    CreateDatabase,   ///< 创建数据库
    DropDatabase,     ///< 删除数据库
    CreateTable,      ///< 创建表
    DropTable,        ///< 删除表
    AlterTable,       ///< 修改表结构
    Insert,           ///< 插入记录
    Delete,           ///< 删除记录
    Update            ///< 更新记录
};

/**
 * @class LogBlock
 * @brief 数据库日志块类
 * @details 封装单条数据库操作日志的完整信息，包括操作类型、目标数据库/表、
 *          操作前后的数据快照以及时间戳。支持 JSON 序列化与反序列化，
 *          用于备份恢复场景的日志持久化。
 * @author NAPH130
 */
class LogBlock
{
public:
    LogBlock();

    // ---------- Getter ----------
    std::int64_t getOperationId() const;
    const DateTime &getTimestamp() const;
    const std::string &getDatabaseName() const;
    const std::string &getTableName() const;
    DbLogOperationType getOperationType() const;
    const std::string &getBeforeData() const;
    const std::string &getAfterData() const;
    const std::string &getSqlText() const;

    // ---------- Setter ----------
    void setOperationId(std::int64_t operationId);
    void setTimestamp(const DateTime &timestamp);
    void setDatabaseName(const std::string &databaseName);
    void setTableName(const std::string &tableName);
    void setOperationType(DbLogOperationType operationType);
    void setBeforeData(const std::string &beforeData);
    void setAfterData(const std::string &afterData);
    void setSqlText(const std::string &sqlText);

    // ---------- JSON 序列化/反序列化 ----------
    /**
     * @brief 将当前 LogBlock 序列化为 JSON 对象
     * @author NAPH130
     * @return nlohmann::json 序列化结果
     */
    nlohmann::json toJson() const;

    /**
     * @brief 从 JSON 对象反序列化为 LogBlock
     * @author NAPH130
     * @param jsonData 待解析的 JSON 对象
     * @param outBlock 输出 LogBlock
     * @return 是否解析成功
     */
    static bool fromJson(const nlohmann::json &jsonData, LogBlock &outBlock);

    /**
     * @brief 将 LogBlock 序列化为 JSON 字符串
     * @author NAPH130
     * @return JSON 字符串
     */
    std::string toJsonString() const;

    /**
     * @brief 从 JSON 字符串反序列化为 LogBlock
     * @author NAPH130
     * @param jsonStr JSON 字符串
     * @param outBlock 输出 LogBlock
     * @return 是否解析成功
     */
    static bool fromJsonString(const std::string &jsonStr, LogBlock &outBlock);

private:
    /**
     * @brief 将 DateTime 转为 JSON 对象
     * @author NAPH130
     * @param dateTime DateTime 对象
     * @return JSON 对象
     */
    static nlohmann::json dateTimeToJson(const DateTime &dateTime);

    /**
     * @brief 从 JSON 对象解析 DateTime
     * @author NAPH130
     * @param jsonData JSON 对象
     * @param outDateTime 输出 DateTime
     * @return 是否解析成功
     */
    static bool jsonToDateTime(const nlohmann::json &jsonData, DateTime &outDateTime);

    /**
     * @brief 操作类型枚举转字符串
     * @author NAPH130
     * @param opType 操作类型
     * @return 字符串表示
     */
    static std::string operationTypeToString(DbLogOperationType opType);

    /**
     * @brief 字符串转操作类型枚举
     * @author NAPH130
     * @param str 字符串
     * @return 操作类型枚举
     */
    static DbLogOperationType stringToOperationType(const std::string &str);

private:
    std::int64_t operationId;   ///< 操作唯一标识（自增序号）
    DateTime timestamp;         ///< 操作发生时间
    std::string databaseName;   ///< 目标数据库名
    std::string tableName;      ///< 目标表名（数据库级操作为空）
    DbLogOperationType operationType; ///< 操作类型
    std::string beforeData;     ///< 操作前数据快照（JSON 字符串）
    std::string afterData;      ///< 操作后数据快照（JSON 字符串）
    std::string sqlText;        ///< 原始 SQL 语句文本
};
