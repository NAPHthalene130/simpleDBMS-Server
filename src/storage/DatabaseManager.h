#pragma once

#include "Table.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace storage {

// 一个轻量的数据库管理器：
// - createDatabase: 创建数据库（对应一个目录）
// - useDatabase:    切换当前数据库
// - createTable:    创建表
// - insert:         简单插入（要求字段数量完全匹配）
//
// 约定：
// - 每个数据库是 rootPath 下的一个子目录
// - 每个表包含两个文件：table.meta / table.data
// - 第一列默认视为主键，并由 B 树建立索引
class DatabaseManager {
public:
    explicit DatabaseManager(std::filesystem::path rootPath = "./data");

    void createDatabase(const std::string& dbName);
    void useDatabase(const std::string& dbName);

    void createTable(const std::string& tableName,
                     const std::vector<std::string>& columns);

    void insert(const std::string& tableName,
                const std::vector<std::string>& values);

    bool hasSelectedDatabase() const;
    std::string currentDatabaseName() const;

private:
    std::filesystem::path rootPath_;
    std::filesystem::path currentDbPath_;
    std::string currentDbName_;

    std::unordered_map<std::string, std::shared_ptr<Table>> openedTables_;

    std::filesystem::path databasePathOf(const std::string& dbName) const;
    void ensureDatabaseSelected() const;
    std::shared_ptr<Table> openTable(const std::string& tableName);
};

} // namespace storage
