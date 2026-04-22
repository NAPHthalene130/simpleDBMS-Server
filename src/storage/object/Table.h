#pragma once

#include "BTree.h"
#include "StorageCommon.h"

#include <filesystem>
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
     * @brief 追加一行数据到数据文件
     * @author Startale
     * @param values 行数据
     */
    void appendData(const std::vector<std::string>& values) const;

    /**
     * @brief 将已有记录加载到内存索引
     * @author Startale
     */
    void loadAllRowsIntoIndex();

    /**
     * @brief 生成主键值
     * @author Startale
     * @param values 行数据
     * @return 主键字符串
     */
    std::string makePrimaryKey(const std::vector<std::string>& values) const;
};

} // namespace storage
