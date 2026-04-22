#include "Table.h"

#include <cstdint>
#include <fstream>

namespace storage {

Table::Table(std::filesystem::path dbPath, TableSchema schema)
    : dbPath_(std::move(dbPath)), schema_(std::move(schema)), index_(2) {}

Table Table::create(const std::filesystem::path& dbPath,
                    const std::string& tableName,
                    const std::vector<std::string>& columns) {
    ensure(!tableName.empty(), "table name cannot be empty");
    ensure(!columns.empty(), "table must contain at least one column");

    Table table(dbPath, TableSchema{tableName, columns});

    ensure(!std::filesystem::exists(table.metaFilePath()), "table already exists: " + tableName);
    table.flushMeta();

    std::ofstream dataOfs(table.dataFilePath(), std::ios::app);
    ensure(dataOfs.good(), "failed to create table data file: " + table.dataFilePath().string());

    std::ofstream integrityOfs(table.integrityFilePath(), std::ios::app);
    ensure(integrityOfs.good(), "failed to create table integrity file: " + table.integrityFilePath().string());

    std::ofstream indexOfs(table.indexFilePath(), std::ios::app);
    ensure(indexOfs.good(), "failed to create table index file: " + table.indexFilePath().string());

    return table;
}

Table Table::load(const std::filesystem::path& dbPath,
                  const std::string& tableName) {
    std::filesystem::path metaPath = dbPath / (tableName + ".tdf");
    ensure(std::filesystem::exists(metaPath), "table does not exist: " + tableName);

    std::ifstream ifs(metaPath);
    ensure(ifs.good(), "failed to open table meta file: " + metaPath.string());

    std::string nameLine;
    std::string columnsLine;
    std::getline(ifs, nameLine);
    std::getline(ifs, columnsLine);

    ensure(nameLine.rfind("table=", 0) == 0, "invalid meta format: missing table line");
    ensure(columnsLine.rfind("columns=", 0) == 0, "invalid meta format: missing columns line");

    Table table(dbPath, TableSchema{
        nameLine.substr(6),
        split(columnsLine.substr(8), '|')
    });

    table.loadIndexFromTid();
    return table;
}

void Table::insert(const std::vector<std::string>& values) {
    ensure(values.size() == schema_.columns.size(),
           "column count mismatch, expected " + std::to_string(schema_.columns.size()) +
           ", got " + std::to_string(values.size()));

    const std::string primaryKey = makePrimaryKey(values);
    ensure(!primaryKey.empty(), "primary key (first column) cannot be empty");
    ensure(!containsPrimaryKey(primaryKey), "duplicate primary key: " + primaryKey);

    Row row{values};
    index_.insert(primaryKey, row);
    const std::uint64_t offset = appendDataRow(values);
    appendIndexEntry(primaryKey, offset);
}

bool Table::containsPrimaryKey(const std::string& key) const {
    return index_.contains(key);
}

std::filesystem::path Table::metaFilePath() const {
    return dbPath_ / (schema_.name + ".tdf");
}

std::filesystem::path Table::dataFilePath() const {
    return dbPath_ / (schema_.name + ".trd");
}

std::filesystem::path Table::integrityFilePath() const {
    return dbPath_ / (schema_.name + ".tic");
}

std::filesystem::path Table::indexFilePath() const {
    return dbPath_ / (schema_.name + ".tid");
}

void Table::flushMeta() const {
    std::ofstream ofs(metaFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to write table meta file: " + metaFilePath().string());

    ofs << "table=" << schema_.name << '\n';
    ofs << "columns=" << join(schema_.columns, "|") << '\n';
}

std::uint64_t Table::appendDataRow(const std::vector<std::string>& values) const {
    std::ofstream ofs(dataFilePath(), std::ios::app);
    ensure(ofs.good(), "failed to open table data file: " + dataFilePath().string());
    const std::uint64_t offset = static_cast<std::uint64_t>(ofs.tellp());
    ofs << "ROW|" << join(values, "|") << '\n';
    return offset;
}

void Table::appendIndexEntry(const std::string& key, std::uint64_t offset) const {
    std::ofstream ofs(indexFilePath(), std::ios::app);
    ensure(ofs.good(), "failed to open table index file: " + indexFilePath().string());
    ofs << key << "|" << offset << '\n';
}

void Table::loadIndexFromTid() {
    index_.clear();

    std::ifstream ifs(indexFilePath());
    if (!ifs.good()) {
        rebuildIndexFromData();
        return;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        const std::size_t sepPos = line.find('|');
        if (sepPos == std::string::npos || sepPos == 0) {
            continue;
        }
        const std::string key = line.substr(0, sepPos);
        index_.insert(key, Row{{key}});
    }
}

void Table::rebuildIndexFromData() {
    index_.clear();

    std::ifstream ifs(dataFilePath());
    if (!ifs.good()) {
        return;
    }
    std::ofstream tidOfs(indexFilePath(), std::ios::trunc);
    ensure(tidOfs.good(), "failed to rebuild table index file: " + indexFilePath().string());

    std::string line;
    std::uint64_t offset = 0;
    while (std::getline(ifs, line)) {
        const std::uint64_t lineStartOffset = offset;
        offset += static_cast<std::uint64_t>(line.size()) + 1;

        if (line.empty()) {
            continue;
        }
        if (line.rfind("ROW|", 0) != 0) {
            continue;
        }
        Row row = deserializeRow(line.substr(4));
        if (row.values.empty()) {
            continue;
        }
        const std::string key = row.values.front();
        index_.insert(key, Row{{key}});
        tidOfs << key << "|" << lineStartOffset << '\n';
    }
}

std::string Table::makePrimaryKey(const std::vector<std::string>& values) const {
    return values.empty() ? std::string() : values.front();
}

} // namespace storage
