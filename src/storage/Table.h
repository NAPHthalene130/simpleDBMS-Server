#pragma once

#include "BTree.h"
#include "StorageCommon.h"

#include <filesystem>
#include <string>
#include <vector>

namespace storage {

class Table {
public:
    Table() = default;
    Table(std::filesystem::path dbPath, TableSchema schema);

    static Table create(const std::filesystem::path& dbPath,
                        const std::string& tableName,
                        const std::vector<std::string>& columns);

    static Table load(const std::filesystem::path& dbPath,
                      const std::string& tableName);

    void insert(const std::vector<std::string>& values);

    const TableSchema& schema() const { return schema_; }
    bool containsPrimaryKey(const std::string& key) const;

private:
    std::filesystem::path dbPath_;
    TableSchema schema_;
    BTree<std::string, Row> index_{2};

    std::filesystem::path metaFilePath() const;
    std::filesystem::path dataFilePath() const;

    void flushMeta() const;
    void appendData(const std::vector<std::string>& values) const;
    void loadAllRowsIntoIndex();
    std::string makePrimaryKey(const std::vector<std::string>& values) const;
};

} // namespace storage
