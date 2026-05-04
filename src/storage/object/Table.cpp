#include "Table.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace storage {

namespace {

bool tryParseNumber(const std::string& text, double& value) {
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || errno == ERANGE) {
        return false;
    }
    value = parsed;
    return true;
}

} // namespace

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
    indexOfs.close();

    table.flushIntegrityMeta();
    table.initializeTidFile();

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
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("table=", 0) == 0) {
            nameLine = line;
        } else if (line.rfind("columns=", 0) == 0) {
            columnsLine = line;
        }
    }

    ensure(!nameLine.empty(), "invalid meta format: missing table line");
    ensure(!columnsLine.empty(), "invalid meta format: missing columns line");

    std::vector<std::string> parsedColumns;
    for (const auto& colMeta : split(columnsLine.substr(8), '|')) {
        const auto sepPos = colMeta.find(':');
        parsedColumns.push_back(sepPos == std::string::npos ? colMeta : colMeta.substr(0, sepPos));
    }

    Table table(dbPath, TableSchema{nameLine.substr(6), parsedColumns});

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

bool Table::updateByPrimaryKey(const std::string& primaryKey,
                               const std::vector<std::string>& newValues) {
    if (primaryKey.empty() || newValues.empty()) {
        return false;
    }
    ensure(newValues.size() == schema_.columns.size(),
           "column count mismatch, expected " + std::to_string(schema_.columns.size()) +
           ", got " + std::to_string(newValues.size()));

    std::vector<Row> rows = readAllDataRows();
    std::size_t hitIndex = rows.size();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].values.empty() && rows[i].values.front() == primaryKey) {
            hitIndex = i;
            break;
        }
    }
    if (hitIndex == rows.size()) {
        return false;
    }

    const std::string newPrimaryKey = makePrimaryKey(newValues);
    if (newPrimaryKey.empty()) {
        return false;
    }
    if (newPrimaryKey != primaryKey) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (i == hitIndex || rows[i].values.empty()) {
                continue;
            }
            if (rows[i].values.front() == newPrimaryKey) {
                return false;
            }
        }
    }

    rows[hitIndex] = Row{newValues};
    rewriteDataRows(rows);
    rebuildIndexFromData();
    return true;
}

bool Table::deleteByPrimaryKey(const std::string& primaryKey) {
    if (primaryKey.empty()) {
        return false;
    }

    std::vector<Row> rows = readAllDataRows();
    std::vector<Row> keptRows;
    keptRows.reserve(rows.size());
    bool deleted = false;
    for (const auto& row : rows) {
        if (!deleted && !row.values.empty() && row.values.front() == primaryKey) {
            deleted = true;
            continue;
        }
        keptRows.push_back(row);
    }
    if (!deleted) {
        return false;
    }

    rewriteDataRows(keptRows);
    rebuildIndexFromData();
    return true;
}

std::vector<Row> Table::select(const std::vector<std::string>& targetColumns,
                               const std::vector<WhereCondition>& whereConditions) const {
    std::vector<std::size_t> projectedIndexes;
    const bool selectAll = targetColumns.empty()
                           || (targetColumns.size() == 1 && targetColumns.front() == "*");
    if (selectAll) {
        projectedIndexes.resize(schema_.columns.size());
        for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
            projectedIndexes[i] = i;
        }
    } else {
        projectedIndexes.reserve(targetColumns.size());
        for (const auto& column : targetColumns) {
            projectedIndexes.push_back(columnIndex(column));
        }
    }

    std::ifstream ifs(dataFilePath());
    ensure(ifs.good(), "failed to open table data file: " + dataFilePath().string());

    std::vector<Row> result;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("ROW|", 0) != 0) {
            continue;
        }

        Row row = deserializeRow(line.substr(4));
        if (row.values.size() != schema_.columns.size()) {
            continue;
        }
        if (!matchWhere(row, whereConditions)) {
            continue;
        }

        Row projected;
        projected.values.reserve(projectedIndexes.size());
        for (const auto index : projectedIndexes) {
            projected.values.push_back(row.values[index]);
        }
        result.push_back(std::move(projected));
    }
    return result;
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

    ofs << "schema_version=2\n";
    ofs << "table=" << schema_.name << '\n';
    std::vector<std::string> columnMetas;
    columnMetas.reserve(schema_.columns.size());
    for (const auto& column : schema_.columns) {
        columnMetas.push_back(column + ":TEXT");
    }
    ofs << "columns=" << join(columnMetas, "|") << '\n';
    ofs << "primary_key=" << schema_.columns.front() << '\n';
    ofs << "index_definitions=PRIMARY(" << schema_.columns.front() << "):BTREE:" << schema_.name << ".tid\n";
}

void Table::flushIntegrityMeta() const {
    std::ofstream ofs(integrityFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to write table integrity file: " + integrityFilePath().string());

    ofs << "constraints_version=1\n";
    ofs << "constraint=PRIMARY_KEY(" << schema_.columns.front() << ")\n";
    ofs << "constraint=NOT_NULL(" << schema_.columns.front() << ")\n";
    ofs << "index=PRIMARY:" << schema_.name << ".tid\n";
}

std::uint64_t Table::appendDataRow(const std::vector<std::string>& values) const {
    std::ofstream ofs(dataFilePath(), std::ios::app);
    ensure(ofs.good(), "failed to open table data file: " + dataFilePath().string());
    const std::uint64_t offset = static_cast<std::uint64_t>(ofs.tellp());
    ofs << "ROW|" << join(values, "|") << '\n';
    return offset;
}

void Table::appendIndexEntry(const std::string& key, std::uint64_t offset) {
    std::ofstream ofs(indexFilePath(), std::ios::app);
    ensure(ofs.good(), "failed to open table index file: " + indexFilePath().string());
    const std::uint32_t pageId = nextPageId_++;
    ofs << "PAGE|" << pageId << "|leaf=1|parent=" << rootPageId_ << "|entry_count=1\n";
    ofs << "ENTRY|" << key << "|" << offset << '\n';
    ofs << "ENDPAGE\n";
}

void Table::loadIndexFromTid() {
    index_.clear();
    if (tryLoadPagedTid()) {
        return;
    }

    // Fallback: legacy "key|offset" format.
    std::ifstream ifs(indexFilePath());
    if (ifs.good()) {
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
    rebuildIndexFromData();
}

void Table::rebuildIndexFromData() {
    index_.clear();

    std::ifstream ifs(dataFilePath());
    if (!ifs.good()) {
        return;
    }
    initializeTidFile();
    std::ofstream tidOfs(indexFilePath(), std::ios::app);
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
        const std::uint32_t pageId = nextPageId_++;
        tidOfs << "PAGE|" << pageId << "|leaf=1|parent=" << rootPageId_ << "|entry_count=1\n";
        tidOfs << "ENTRY|" << key << "|" << lineStartOffset << '\n';
        tidOfs << "ENDPAGE\n";
    }
}

void Table::initializeTidFile() {
    std::ofstream ofs(indexFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to initialize table index file: " + indexFilePath().string());
    rootPageId_ = 1;
    nextPageId_ = 2;

    ofs << "TID_PAGED_V1\n";
    ofs << "page_size=4096\n";
    ofs << "root_page=" << rootPageId_ << '\n';
    ofs << "PAGE|1|leaf=1|parent=0|entry_count=0\n";
    ofs << "ENDPAGE\n";
}

bool Table::tryLoadPagedTid() {
    std::ifstream ifs(indexFilePath());
    if (!ifs.good()) {
        return false;
    }

    std::string magic;
    std::getline(ifs, magic);
    if (magic != "TID_PAGED_V1") {
        return false;
    }

    rootPageId_ = 1;
    nextPageId_ = 2;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("root_page=", 0) == 0) {
            rootPageId_ = static_cast<std::uint32_t>(std::stoul(line.substr(10)));
            continue;
        }
        if (line.rfind("PAGE|", 0) == 0) {
            std::vector<std::string> parts = split(line, '|');
            if (parts.size() >= 2) {
                const std::uint32_t pageId = static_cast<std::uint32_t>(std::stoul(parts[1]));
                if (pageId >= nextPageId_) {
                    nextPageId_ = pageId + 1;
                }
            }
            continue;
        }
        if (line.rfind("ENTRY|", 0) == 0) {
            std::vector<std::string> parts = split(line, '|');
            if (parts.size() < 3 || parts[1].empty()) {
                continue;
            }
            index_.insert(parts[1], Row{{parts[1]}});
        }
    }
    return true;
}

std::vector<Row> Table::readAllDataRows() const {
    std::vector<Row> rows;
    std::ifstream ifs(dataFilePath());
    if (!ifs.good()) {
        return rows;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("ROW|", 0) != 0) {
            continue;
        }
        Row row = deserializeRow(line.substr(4));
        if (row.values.size() != schema_.columns.size()) {
            continue;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void Table::rewriteDataRows(const std::vector<Row>& rows) const {
    std::ofstream ofs(dataFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to rewrite table data file: " + dataFilePath().string());
    for (const auto& row : rows) {
        ofs << "ROW|" << serializeRow(row) << '\n';
    }
}

std::size_t Table::columnIndex(const std::string& columnName) const {
    const auto it = std::find(schema_.columns.begin(), schema_.columns.end(), columnName);
    ensure(it != schema_.columns.end(), "unknown column: " + columnName);
    return static_cast<std::size_t>(std::distance(schema_.columns.begin(), it));
}

bool Table::matchWhere(const Row& row, const std::vector<WhereCondition>& whereConditions) const {
    for (const auto& condition : whereConditions) {
        const std::size_t idx = columnIndex(condition.column);
        if (idx >= row.values.size()) {
            return false;
        }
        if (!compareValue(row.values[idx], condition.op, condition.value)) {
            return false;
        }
    }
    return true;
}

bool Table::compareValue(const std::string& left, CompareOp op, const std::string& right) {
    if (op == CompareOp::LIKE) {
        return likeMatch(left, right);
    }

    double leftNum = 0.0;
    double rightNum = 0.0;
    const bool leftIsNum = tryParseNumber(left, leftNum);
    const bool rightIsNum = tryParseNumber(right, rightNum);

    if (leftIsNum && rightIsNum) {
        switch (op) {
            case CompareOp::EQ:
                return leftNum == rightNum;
            case CompareOp::NE:
                return leftNum != rightNum;
            case CompareOp::GT:
                return leftNum > rightNum;
            case CompareOp::GE:
                return leftNum >= rightNum;
            case CompareOp::LT:
                return leftNum < rightNum;
            case CompareOp::LE:
                return leftNum <= rightNum;
        case CompareOp::LIKE:
            return likeMatch(left, right);
        }
    }

    switch (op) {
        case CompareOp::EQ:
            return left == right;
        case CompareOp::NE:
            return left != right;
        case CompareOp::GT:
            return left > right;
        case CompareOp::GE:
            return left >= right;
        case CompareOp::LT:
            return left < right;
        case CompareOp::LE:
            return left <= right;
        case CompareOp::LIKE:
            return likeMatch(left, right);
    }
    return false;
}

bool Table::likeMatch(const std::string& text, const std::string& pattern) {
    // Support SQL-like wildcard '%' (zero or more chars).
    std::size_t t = 0;
    std::size_t p = 0;
    std::size_t star = std::string::npos;
    std::size_t match = 0;

    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == text[t])) {
            ++t;
            ++p;
            continue;
        }
        if (p < pattern.size() && pattern[p] == '%') {
            star = p++;
            match = t;
            continue;
        }
        if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
            continue;
        }
        return false;
    }

    while (p < pattern.size() && pattern[p] == '%') {
        ++p;
    }
    return p == pattern.size();
}

std::string Table::makePrimaryKey(const std::vector<std::string>& values) const {
    return values.empty() ? std::string() : values.front();
}

} // namespace storage
