#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "models/dbLog/LogBlock.h"

class Core;

/**
 * @class DbLogManager
 * @brief 数据库日志备份恢复模块总管理器
 * @details 负责统一管理数据库操作日志的记录、持久化与基于时间点的数据库恢复。
 *          遵循 "Write-Ahead Log" 思想，所有写操作在执行前/后均记录快照信息，
 *          以便在需要时通过回放日志将数据库恢复到指定时间点状态。
 * @author NAPH130
 */
class DbLogManager
{
public:
    /**
     * @brief 构造函数
     * @author NAPH130
     * @param core 服务端核心对象指针
     */
    explicit DbLogManager(Core *core);

    /**
     * @brief 析构函数
     * @author NAPH130
     */
    ~DbLogManager();

    // ── 日志记录接口 ──

    /**
     * @brief 记录创建数据库操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param sqlText 原始 SQL 语句
     */
    void logCreateDatabase(const std::string &databaseName,
                           const std::string &sqlText);

    /**
     * @brief 记录删除数据库操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param beforeData 删除前的数据库元信息快照
     * @param sqlText 原始 SQL 语句
     */
    void logDropDatabase(const std::string &databaseName,
                         const std::string &beforeData,
                         const std::string &sqlText);

    /**
     * @brief 记录创建表操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param tableName 表名称
     * @param afterData 创建后的表结构快照
     * @param sqlText 原始 SQL 语句
     */
    void logCreateTable(const std::string &databaseName,
                        const std::string &tableName,
                        const std::string &afterData,
                        const std::string &sqlText);

    /**
     * @brief 记录删除表操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param tableName 表名称
     * @param beforeData 删除前的表结构快照
     * @param sqlText 原始 SQL 语句
     */
    void logDropTable(const std::string &databaseName,
                      const std::string &tableName,
                      const std::string &beforeData,
                      const std::string &sqlText);

    /**
     * @brief 记录修改表结构操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param tableName 表名称
     * @param beforeData 修改前的表结构快照
     * @param afterData 修改后的表结构快照
     * @param sqlText 原始 SQL 语句
     */
    void logAlterTable(const std::string &databaseName,
                       const std::string &tableName,
                       const std::string &beforeData,
                       const std::string &afterData,
                       const std::string &sqlText);

    /**
     * @brief 记录插入记录操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param tableName 表名称
     * @param afterData 插入后的记录快照
     * @param sqlText 原始 SQL 语句
     */
    void logInsert(const std::string &databaseName,
                   const std::string &tableName,
                   const std::string &afterData,
                   const std::string &sqlText);

    /**
     * @brief 记录删除记录操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param tableName 表名称
     * @param beforeData 删除前的记录快照
     * @param sqlText 原始 SQL 语句
     */
    void logDelete(const std::string &databaseName,
                   const std::string &tableName,
                   const std::string &beforeData,
                   const std::string &sqlText);

    /**
     * @brief 记录更新记录操作
     * @author NAPH130
     * @param databaseName 数据库名称
     * @param tableName 表名称
     * @param beforeData 更新前的记录快照
     * @param afterData 更新后的记录快照
     * @param sqlText 原始 SQL 语句
     */
    void logUpdate(const std::string &databaseName,
                   const std::string &tableName,
                   const std::string &beforeData,
                   const std::string &afterData,
                   const std::string &sqlText);

    // ── 恢复接口 ──

    /**
     * @brief 将指定数据库恢复到目标时间点
     * @author NAPH130
     * @param databaseName 目标数据库名称
     * @param targetTime 目标恢复时间点
     * @return 是否恢复成功
     * @note 恢复逻辑：从日志文件中读取该数据库的所有日志记录，
     *       按时间顺序回放到目标时间点，重建数据库状态。
     */
    bool dbRecover(const std::string &databaseName, const DateTime &targetTime);

    /**
     * @brief 获取指定数据库的所有日志记录
     * @author NAPH130
     * @param databaseName 数据库名称
     * @return 日志记录列表（按时间升序）
     */
    std::vector<LogBlock> getLogsForDatabase(const std::string &databaseName);

    /**
     * @brief 获取当前日志操作序号
     * @author NAPH130
     * @return 操作序号
     */
    std::int64_t getCurrentOperationId() const;

private:
    /**
     * @brief 将单条日志写入文件
     * @author NAPH130
     * @param block 日志块
     */
    void appendLog(const LogBlock &block);

    /**
     * @brief 获取下一个操作序号（线程安全）
     * @author NAPH130
     * @return 操作序号
     */
    std::int64_t nextOperationId();

    /**
     * @brief 获取日志文件路径
     * @author NAPH130
     * @return 日志文件路径
     */
    std::string getLogFilePath() const;

    /**
     * @brief 从当前系统时间构造 DateTime
     * @author NAPH130
     * @return 当前时间 DateTime
     */
    static DateTime buildCurrentDateTime();

    /**
     * @brief 比较两个 DateTime 的大小
     * @author NAPH130
     * @param a DateTime a
     * @param b DateTime b
     * @return a <= b 则为 true
     */
    static bool dateTimeLessOrEqual(const DateTime &a, const DateTime &b);

private:
    Core *core;
    std::mutex logFileMutex;    ///< 日志文件写入互斥锁
    std::int64_t operationIdCounter; ///< 操作序号计数器
};
