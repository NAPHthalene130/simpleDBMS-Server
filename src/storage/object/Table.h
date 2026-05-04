#pragma once

#include "BTree.h"
#include "StorageCommon.h"

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace storage {

/**
 * @class Table
 * @brief 数据表对象
 * @details 封装表结构定义、记录写入、索引维护与物理文件路径管理。
 * @author Startale
 */
class Table {
public:
    enum class CompareOp {
        EQ,
        NE,
        GT,
        GE,
        LT,
        LE,
        LIKE
    };

    struct WhereCondition {
        std::string column;
        CompareOp op = CompareOp::EQ;
        std::string value;
    };

    /**
     * @brief 默认构造函数
     * @author Startale
     */
    Table() = default;

    /**
     * @brief 构造数据表对象
     * @author Startale
     * @param dbPath 数据库目录路径
     * @param schema 表结构信息
     */
    Table(std::filesystem::path dbPath, TableSchema schema);

    /**
     * @brief 创建新表及其物理文件
     * @author Startale
     * @param dbPath 数据库目录路径
     * @param tableName 表名
     * @param columns 列名集合
     * @return 创建后的表对象
     */
    static Table create(const std::filesystem::path& dbPath,
                        const std::string& tableName,
                        const std::vector<std::string>& columns);

    /**
     * @brief 加载已有数据表
     * @author Startale
     * @param dbPath 数据库目录路径
     * @param tableName 表名
     * @return 加载后的表对象
     */
    static Table load(const std::filesystem::path& dbPath,
                      const std::string& tableName);

    /**
     * @brief 插入一行记录
     * @author Startale
     * @param values 行数据
     */
    void insert(const std::vector<std::string>& values);

    /**
     * @brief 按主键更新整行记录
     * @author Startale
     * @param primaryKey 主键值
     * @param newValues 新行数据
     * @return 是否更新成功
     */
    bool updateByPrimaryKey(const std::string& primaryKey,
                            const std::vector<std::string>& newValues);

    /**
     * @brief 按主键删除记录
     * @author Startale
     * @param primaryKey 主键值
     * @return 是否删除成功
     */
    bool deleteByPrimaryKey(const std::string& primaryKey);

    /**
     * @brief 按列投影并按条件过滤查询数据
     * @author Startale
     * @param targetColumns 目标列名列表，传空或 {"*"} 表示返回全部列
     * @param whereConditions where 条件列表，多个条件按 AND 关系处理
     * @return 结果行集合
     */
    std::vector<Row> select(const std::vector<std::string>& targetColumns,
                            const std::vector<WhereCondition>& whereConditions = {}) const;

    /**
     * @brief 获取表结构
     * @author Startale
     * @return 表结构对象
     */
    const TableSchema& schema() const { return schema_; }

    /**
     * @brief 检查主键是否已存在
     * @author Startale
     * @param key 主键值
     * @return 是否存在
     */
    bool containsPrimaryKey(const std::string& key) const;

private:
    std::filesystem::path dbPath_;
    TableSchema schema_;
    BTree<std::string, Row> index_{2};
    std::uint32_t rootPageId_ = 1;
    std::uint32_t nextPageId_ = 2;

    /**
     * @brief 获取表元数据文件路径
     * @author Startale
     * @return 元数据文件路径
     */
    std::filesystem::path metaFilePath() const;

    /**
     * @brief 获取表数据文件路径
     * @author Startale
     * @return 数据文件路径
     */
    std::filesystem::path dataFilePath() const;

    /**
     * @brief 获取完整性文件路径
     * @author Startale
     * @return 完整性文件路径
     */
    std::filesystem::path integrityFilePath() const;

    /**
     * @brief 获取索引文件路径
     * @author Startale
     * @return 索引文件路径
     */
    std::filesystem::path indexFilePath() const;

    /**
     * @brief 持久化表结构到元数据文件
     * @author Startale
     */
    void flushMeta() const;

    /**
     * @brief 持久化约束与索引元数据到 .tic 文件
     * @author Startale
     */
    void flushIntegrityMeta() const;

    /**
     * @brief 追加一行数据记录到 .trd 文件
     * @author Startale
     * @param values 行数据
     * @return 写入前的字节偏移
     */
    std::uint64_t appendDataRow(const std::vector<std::string>& values) const;

    /**
     * @brief 追加主键索引记录到 .tid 文件
     * @author Startale
     * @param key 主键值
     * @param offset 对应数据在 .trd 中的偏移
     */
    void appendIndexEntry(const std::string& key, std::uint64_t offset);

    /**
     * @brief 从 .tid 恢复内存索引
     * @author Startale
     */
    void loadIndexFromTid();

    /**
     * @brief 将已有记录加载到内存索引
     * @author Startale
     */
    void rebuildIndexFromData();

    /**
     * @brief 初始化页式 .tid 文件头
     * @author Startale
     */
    void initializeTidFile();

    /**
     * @brief 从 .tid 读取并恢复页式索引
     * @author Startale
     * @return 是否成功按页式格式加载
     */
    bool tryLoadPagedTid();

    /**
     * @brief 读取数据文件中的所有行记录
     * @author Startale
     * @return 行记录列表
     */
    std::vector<Row> readAllDataRows() const;

    /**
     * @brief 覆盖写回所有行记录到数据文件
     * @author Startale
     * @param rows 行记录列表
     */
    void rewriteDataRows(const std::vector<Row>& rows) const;

    /**
     * @brief 获取列名对应下标
     * @author Startale
     * @param columnName 列名
     * @return 列下标
     */
    std::size_t columnIndex(const std::string& columnName) const;

    /**
     * @brief 检查行是否满足 where 条件
     * @author Startale
     * @param row 行数据
     * @param whereConditions 条件列表
     * @return 是否满足
     */
    bool matchWhere(const Row& row, const std::vector<WhereCondition>& whereConditions) const;

    /**
     * @brief 比较两个字段值
     * @author Startale
     * @param left 左值
     * @param op 比较操作符
     * @param right 右值
     * @return 比较结果
     */
    static bool compareValue(const std::string& left, CompareOp op, const std::string& right);

    /**
     * @brief LIKE 模式匹配
     * @author Startale
     * @param text 待匹配文本
     * @param pattern LIKE 模式，支持 % 通配
     * @return 是否匹配
     */
    static bool likeMatch(const std::string& text, const std::string& pattern);

    /**
     * @brief 生成主键值
     * @author Startale
     * @param values 行数据
     * @return 主键字符串
     */
    std::string makePrimaryKey(const std::vector<std::string>& values) const;
};

} // namespace storage
