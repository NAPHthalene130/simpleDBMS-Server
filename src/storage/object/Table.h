#pragma once

// Windows header may define IN as macro, breaking CompareOp enum
#ifdef IN
#undef IN
#endif


#include "BTree.h"
#include "StorageCommon.h"

#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <map>
#include <string>
#include <unordered_set>
#include <unordered_map>
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
        LIKE,
        IN,
        BETWEEN
    };

    enum class LogicalOp {
        AND,
        OR
    };

    enum class AggregateOp {
        COUNT,
        SUM,
        AVG,
        MIN,
        MAX
    };

    struct WhereCondition {
        std::string column;
        CompareOp op = CompareOp::EQ;
        std::string value;
        std::string secondValue;
        std::vector<std::string> values;
    };

    struct AggregateExpr {
        AggregateOp op = AggregateOp::COUNT;
        std::string column = "*";
    };

    struct ConditionNode {
        bool isLeaf = true;
        WhereCondition condition;
        LogicalOp logicalOp = LogicalOp::AND;
        std::shared_ptr<ConditionNode> left;
        std::shared_ptr<ConditionNode> right;
    };

    struct SelectOptions {
        std::string orderByColumn;
        bool orderByDesc = false;
        bool hasLimit = false;
        std::size_t limit = 0;
    };

    enum class ConstraintType {
        NOT_NULL,
        UNIQUE,
        DEFAULT_VALUE,
        CHECK_CONSTRAINT
    };

    struct ColumnConstraintSpec {
        std::string column;
        bool notNull = false;
        bool unique = false;
        bool hasDefault = false;
        std::string defaultValue;
        bool hasCheck = false;
        std::string checkExpr;  // format: "op|value" e.g. ">=|18" or "<|100"
    };

    struct ColumnDefinition {
        std::string name;
        ColumnConstraintSpec constraints;
        DataType dataType = DataType::TEXT;
        std::uint16_t varcharLen = 0;
    };

    struct QueryConstraint {
        std::string column;
        ConstraintType type = ConstraintType::NOT_NULL;
        bool satisfy = true;
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
                        const std::vector<std::string>& columns,
                        const std::vector<ColumnMeta>& columnMetas = {});

    static Table create(const std::filesystem::path& dbPath,
                        const std::string& tableName,
                        const std::vector<ColumnDefinition>& columns);

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
     * @brief 压缩 .trd 数据文件，整理页内碎片并回收空间
     * @author Startale
     * @return 移除的已删除槽位数
     */
    std::size_t compact();

    /**
     * @brief 按列投影并按条件过滤查询数据
     * @author Startale
     * @param targetColumns 目标列名列表，传空或 {"*"} 表示返回全部列
     * @param whereConditions where 条件列表，多个条件按 AND 关系处理
     * @return 结果行集合
     */
    std::vector<Row> select(const std::vector<std::string>& targetColumns,
                            const std::vector<WhereCondition>& whereConditions = {},
                            const SelectOptions& options = SelectOptions()) const;

    std::vector<Row> select(const std::vector<std::string>& targetColumns,
                            const std::shared_ptr<ConditionNode>& whereTree,
                            const SelectOptions& options = SelectOptions()) const;

    std::vector<Row> select(const std::vector<std::string>& targetColumns,
                            const std::vector<WhereCondition>& whereConditions,
                            const std::vector<QueryConstraint>& queryConstraints,
                            const SelectOptions& options = SelectOptions()) const;

    std::vector<Row> select(const std::vector<std::string>& targetColumns,
                            const std::shared_ptr<ConditionNode>& whereTree,
                            const std::vector<QueryConstraint>& queryConstraints,
                            const SelectOptions& options = SelectOptions()) const;

    std::vector<std::string> aggregate(const std::vector<AggregateExpr>& expressions,
                                       const std::vector<WhereCondition>& whereConditions = {}) const;

    std::vector<std::string> aggregate(const std::vector<AggregateExpr>& expressions,
                                       const std::shared_ptr<ConditionNode>& whereTree) const;

    bool addColumnConstraint(const ColumnConstraintSpec& spec);
    bool addColumnConstraints(const std::vector<ColumnConstraintSpec>& specs);
    std::vector<ColumnConstraintSpec> getColumnConstraints() const;

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
    struct ColumnIndex {
        std::multimap<std::string, std::uint64_t> entries;
        std::filesystem::path filePath;
        bool active = false;

        void add(const std::string& value, std::uint64_t offset) {
            entries.emplace(value, offset);
        }
        void remove(const std::string& value, std::uint64_t offset) {
            auto range = entries.equal_range(value);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == offset) { entries.erase(it); break; }
            }
        }
        void save(const std::filesystem::path& path);
        void load(const std::filesystem::path& path);
        bool lookup(const std::string& value, Table::CompareOp op,
                    const std::string& secondValue, const std::vector<std::string>& values,
                    std::vector<std::uint64_t>& offsets) const;
    };

    struct ConstraintValidator {
        const Table& table;

        explicit ConstraintValidator(const Table& t) : table(t) {}

        std::vector<std::string> normalize(const std::vector<std::string>& values) const;
        void check(const std::vector<std::string>& values, const std::string* skipPrimaryKey = nullptr) const;
        bool checkNewConstraint(const ColumnConstraintSpec& spec) const;
    };

    struct DataPageManager {
        Table& table;

        explicit DataPageManager(Table& t) : table(t) {}

        TupleRef allocate(const std::vector<std::string>& values);
        bool read(TupleRef ref, Row& out) const;
        bool markDeleted(TupleRef ref);
        std::vector<Row> scanAll() const;
        void scan(std::function<void(TupleRef, const Row&)> visitor) const;
        bool compactPage(std::uint32_t pageId);
        std::size_t compactAll();
    };

    std::filesystem::path dbPath_;
    TableSchema schema_;
    BTree<std::string, Row> index_{2};
    std::unordered_map<std::string, std::uint64_t> primaryKeyOffsets_;
    std::map<std::string, std::uint64_t> primaryKeyOffsetsOrdered_;
    std::unordered_map<std::string, ColumnConstraintSpec> constraintsByColumn_;
    std::unordered_map<std::string, ColumnIndex> secondaryIndexes_;
    DataPageManager dataPages_{*this};
    std::uint32_t rootPageId_ = 1;
    std::uint32_t nextPageId_ = 2;
    using TidNodeRef = BTree<std::string, Row>::TidNodeRef;

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
    std::filesystem::path nonPrimaryIndexFilePath(const std::string& column) const;

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
    void loadConstraintsFromIntegrityMeta();

    /**
     * @brief 将内存 BTree 索引刷盘为页式 .tid 文件
     * @author Startale
     */
    void syncIndexPages();
    void writeHeader(std::ostream& os);

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
    bool readRowByOffset(std::uint64_t offset, Row& row) const;

    std::vector<std::string> normalizeInputValues(const std::vector<std::string>& values) const;
    bool validateConstraintForExistingRows(const ColumnConstraintSpec& spec) const;
    void enforceRowConstraints(const std::vector<std::string>& values,
                               const std::string* skipPrimaryKey = nullptr) const;
    bool matchQueryConstraints(const Row& row,
                               const std::vector<QueryConstraint>& queryConstraints,
                               const std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>>&
                                   uniqueCounters) const;
    std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>>
    buildUniqueCountersForQuery(const std::vector<QueryConstraint>& queryConstraints) const;

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
    bool matchConditionTree(const Row& row, const std::shared_ptr<ConditionNode>& node) const;
    struct IndexCandidateResult {
        bool constrained = false;
        std::vector<std::uint64_t> offsets;
    };
    bool hasIndexForColumn(const std::string& column) const;
    bool canUseIndexForCondition(const WhereCondition& condition) const;
    struct IndexedLookupRequest {
        std::string column;
        CompareOp op = CompareOp::EQ;
        std::string value;
        std::string secondValue;
        std::vector<std::string> values;
    };
    bool lookupOffsetsByIndexedRequest(const IndexedLookupRequest& request,
                                       std::vector<std::uint64_t>& offsets) const;
    bool lookupOffsetsByPrimaryIndex(const IndexedLookupRequest& request,
                                     std::vector<std::uint64_t>& offsets) const;
    bool lookupOffsetsBySecondaryIndex(const IndexedLookupRequest& request,
                                       std::vector<std::uint64_t>& offsets) const;
    bool lookupOffsetsByIndexedCondition(const WhereCondition& condition,
                                         std::vector<std::uint64_t>& offsets) const;
    IndexCandidateResult collectIndexCandidates(const std::shared_ptr<ConditionNode>& node) const;
    static std::vector<std::uint64_t> mergeOffsetUnion(const std::vector<std::uint64_t>& left,
                                                       const std::vector<std::uint64_t>& right);
    static std::vector<std::uint64_t> mergeOffsetIntersection(const std::vector<std::uint64_t>& left,
                                                              const std::vector<std::uint64_t>& right);

    /**
     * @brief 比较两个字段值
     * @author Startale
     * @param left 左值
     * @param op 比较操作符
     * @param right 右值
     * @return 比较结果
     */
    static bool compareValue(const std::string& left, CompareOp op, const std::string& right);
    static bool compareValue(const std::string& left,
                              const WhereCondition& condition);
    bool compareTyped(std::size_t colIndex, const std::string& left, CompareOp op, const std::string& right) const;

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
