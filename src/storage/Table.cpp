#include "Table.h"

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

    std::ofstream ofs(table.dataFilePath(), std::ios::app);
    ensure(ofs.good(), "failed to create table data file: " + table.dataFilePath().string());

    return table;
}

Table Table::load(const std::filesystem::path& dbPath,
                  const std::string& tableName) {
    std::filesystem::path metaPath = dbPath / (tableName + ".meta");
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

    table.loadAllRowsIntoIndex();
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
    appendData(values);
}

bool Table::containsPrimaryKey(const std::string& key) const {
    return index_.contains(key);
}

std::filesystem::path Table::metaFilePath() const {
    return dbPath_ / (schema_.name + ".meta");
}

std::filesystem::path Table::dataFilePath() const {
    return dbPath_ / (schema_.name + ".data");
}

void Table::flushMeta() const {
    std::ofstream ofs(metaFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to write table meta file: " + metaFilePath().string());

    ofs << "table=" << schema_.name << '\n';
    ofs << "columns=" << join(schema_.columns, "|") << '\n';
}

void Table::appendData(const std::vector<std::string>& values) const {
    std::ofstream ofs(dataFilePath(), std::ios::app);
    ensure(ofs.good(), "failed to open table data file: " + dataFilePath().string());
    ofs << join(values, "|") << '\n';
}

void Table::loadAllRowsIntoIndex() {
    std::ifstream ifs(dataFilePath());
    if (!ifs.good()) {
        return;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) {
            continue;
        }
        Row row = deserializeRow(line);
        if (row.values.empty()) {
            continue;
        }
        index_.insert(row.values.front(), row);
    }
}

std::string Table::makePrimaryKey(const std::vector<std::string>& values) const {
    return values.empty() ? std::string() : values.front();
}

} // namespace storage
