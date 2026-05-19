#include "Table.h"
#include "TableVersionManager.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

#include "storage/manager/FileManager.h"

#include "models/storage/IndexBlock.h"
#include "models/storage/IntegrityBlock.h"

namespace storage {

namespace {

std::string formatDouble(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    std::string out = oss.str();
    while (!out.empty() && out.back() == '0') {
        out.pop_back();
    }
    if (!out.empty() && out.back() == '.') {
        out.pop_back();
    }
    return out.empty() ? "0" : out;
}

std::string encodeConstraintValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (ch == '|') {
            out += "%7C";
        } else if (ch == '\n' || ch == '\r') {
            out += ' ';
        } else {
            out += ch;
        }
    }
    return out;
}

std::string decodeConstraintValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (i + 2 < value.size() && value[i] == '%' && value[i + 1] == '7' && value[i + 2] == 'C') {
            out += '|';
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value) {
    std::array<char, N> result{};
    const auto copyLen = std::min<std::size_t>(value.size(), N - 1);
    std::memcpy(result.data(), value.data(), copyLen);
    return result;
}

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value) {
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

} // namespace

Table::Table(std::filesystem::path dbPath, TableSchema schema)
    : dbPath_(std::move(dbPath)), schema_(std::move(schema)), index_(2) {}

Table Table::create(const std::filesystem::path& dbPath,
                    const std::string& tableName,
                    const std::vector<std::string>& columns,
                    const std::vector<ColumnMeta>& columnMetas) {
    ensure(!tableName.empty(), "table name cannot be empty");
    ensure(!columns.empty(), "table must contain at least one column");
    for (const auto& col : columns) {
        ensure(!col.empty(), "column name cannot be empty");
    }

    Table table(dbPath, TableSchema{tableName, columns, columnMetas});
    if (table.schema_.columnMetas.empty()) {
        table.schema_.columnMetas.resize(columns.size());
    } else if (table.schema_.columnMetas.size() < columns.size()) {
        table.schema_.columnMetas.resize(columns.size());
    }
    for (std::size_t i = 0; i < columns.size(); ++i) {
        ColumnConstraintSpec spec;
        spec.column = columns[i];
        const std::int32_t flags = table.schema_.columnMetas[i].integrities;
        spec.notNull = (flags & 1) != 0;
        spec.unique = (flags & 4) != 0;
        spec.hasDefault = !table.schema_.columnMetas[i].defaultValue.empty();
        spec.defaultValue = table.schema_.columnMetas[i].defaultValue;
        table.constraintsByColumn_[columns[i]] = spec;
    }
    if (!columns.empty()) {
        table.schema_.columnMetas.front().integrities |= (1 | 2 | 4);
        table.constraintsByColumn_[columns.front()].notNull = true;
        table.constraintsByColumn_[columns.front()].unique = true;
    }

    ensure(!std::filesystem::exists(table.metaFilePath()), "table already exists: " + tableName);
    table.flushMeta();

    std::ofstream dataOfs(table.dataFilePath(), std::ios::app);
    ensure(dataOfs.good(), "failed to create table data file: " + table.dataFilePath().string());

    std::ofstream integrityOfs(table.integrityFilePath(), std::ios::app);
    ensure(integrityOfs.good(), "failed to create table integrity file: " + table.integrityFilePath().string());

    std::ofstream indexOfs(table.indexFilePath(), std::ios::app);
    ensure(indexOfs.good(), "failed to create table index file: " + table.indexFilePath().string());
    indexOfs.close();

    for (std::size_t i = 1; i < columns.size(); ++i) {
        std::ofstream secondaryOfs(table.nonPrimaryIndexFilePath(columns[i]), std::ios::app);
        ensure(secondaryOfs.good(),
               "failed to create non-primary index reserve file: "
               + table.nonPrimaryIndexFilePath(columns[i]).string());
        ColumnIndex ci;
        ci.filePath = table.nonPrimaryIndexFilePath(columns[i]);
        ci.active = true;
        table.secondaryIndexes_[columns[i]] = std::move(ci);
    }

    table.flushIntegrityMeta();
    table.syncIndexPages();

    TableVersionManager::initialize(dbPath, tableName);

    return table;
}

Table Table::create(const std::filesystem::path& dbPath,
                    const std::string& tableName,
                    const std::vector<ColumnDefinition>& columns) {
    std::vector<std::string> names;
    std::vector<ColumnMeta> metas;
    names.reserve(columns.size());
    metas.reserve(columns.size());
    for (const auto& col : columns) {
        ensure(!col.name.empty(), "column name cannot be empty");
        names.push_back(col.name);
        metas.push_back(toColumnMeta(col));
    }
    return create(dbPath, tableName, names, metas);
}

Table Table::load(const std::filesystem::path& dbPath,
                  const std::string& tableName) {
    std::filesystem::path metaPath = dbPath / (tableName + ".tdf");
    ensure(std::filesystem::exists(metaPath), "table does not exist: " + tableName);

    std::ifstream ifs(metaPath);
    ensure(ifs.good(), "failed to open table meta file: " + metaPath.string());

    std::string nameLine;
    std::string columnsLine;
    std::string integritiesLine;
    std::string defaultsLine;
    std::map<std::string, std::int64_t> autoIncMap;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("table=", 0) == 0) {
            nameLine = line;
        } else if (line.rfind("columns=", 0) == 0) {
            columnsLine = line;
        } else if (line.rfind("integrities=", 0) == 0) {
            integritiesLine = line;
        } else if (line.rfind("defaults=", 0) == 0) {
            defaultsLine = line;
        } else if (line.rfind("autoInc:", 0) == 0) {
            const auto eqPos = line.find('=', 8);
            if (eqPos != std::string::npos) {
                const std::string col = line.substr(8, eqPos - 8);
                try {
                    autoIncMap[col] = std::stoll(line.substr(eqPos + 1));
                } catch (...) {}
            }
        }
    }

    ensure(!nameLine.empty(), "invalid meta format: missing table line");
    ensure(!columnsLine.empty(), "invalid meta format: missing columns line");

    std::vector<std::string> parsedColumns;
    for (const auto& colMeta : split(columnsLine.substr(8), '|')) {
        const auto sepPos = colMeta.find(':');
        parsedColumns.push_back(sepPos == std::string::npos ? colMeta : colMeta.substr(0, sepPos));
    }

    std::vector<ColumnMeta> columnMetas(parsedColumns.size());
    // Parse types from columns line
    {
        auto typeParts = split(columnsLine.substr(8), '|');
        for (std::size_t i = 0; i < typeParts.size() && i < columnMetas.size(); ++i) {
            const auto sepPos = typeParts[i].find(':');
            if (sepPos == std::string::npos) continue;
            std::string typeStr = typeParts[i].substr(sepPos + 1);
            if (typeStr == "INT") {
                columnMetas[i].dataType = DataType::INT;
            } else if (typeStr == "FLOAT") {
                columnMetas[i].dataType = DataType::FLOAT;
            } else if (typeStr.rfind("VARCHAR(", 0) == 0 && typeStr.back() == ')') {
                columnMetas[i].dataType = DataType::VARCHAR;
                try { columnMetas[i].varcharLen = static_cast<std::uint16_t>(std::stoul(typeStr.substr(8, typeStr.size()-9))); }
                catch (...) { columnMetas[i].varcharLen = 0; }
            }
            // else: TEXT (default)
        }
    }
    if (!integritiesLine.empty()) {
        const auto integList = split(integritiesLine.substr(13), '|');
        for (std::size_t i = 0; i < integList.size() && i < columnMetas.size(); ++i) {
            try {
                columnMetas[i].integrities = std::stoi(integList[i]);
            } catch (...) {
                columnMetas[i].integrities = 0;
            }
        }
    }
    if (!defaultsLine.empty()) {
        const auto defaultList = split(defaultsLine.substr(9), '|');
        for (std::size_t i = 0; i < defaultList.size() && i < columnMetas.size(); ++i) {
            columnMetas[i].defaultValue = defaultList[i];
        }
    }

    Table table(dbPath, TableSchema{nameLine.substr(6), parsedColumns, columnMetas});
    for (std::size_t i = 0; i < parsedColumns.size(); ++i) {
        ColumnConstraintSpec spec;
        spec.column = parsedColumns[i];
        spec.notNull = (table.schema_.columnMetas[i].integrities & 1) != 0;
        spec.unique = (table.schema_.columnMetas[i].integrities & 4) != 0;
        spec.hasDefault = !table.schema_.columnMetas[i].defaultValue.empty();
        spec.defaultValue = table.schema_.columnMetas[i].defaultValue;
        table.constraintsByColumn_[parsedColumns[i]] = spec;
    }

    table.autoIncCounters_ = std::move(autoIncMap);

    table.loadIndexFromTid();
    for (std::size_t i = 1; i < table.schema_.columns.size(); ++i) {
        const std::string& col = table.schema_.columns[i];
        auto nidxPath = table.nonPrimaryIndexFilePath(col);
        if (std::filesystem::exists(nidxPath)) {
            ColumnIndex ci;
            ci.filePath = nidxPath;
            ci.active = true;
            ci.load(nidxPath);
            table.secondaryIndexes_[col] = std::move(ci);
        }
    }
    table.loadConstraintsFromIntegrityMeta();
    if (!table.schema_.columns.empty()) {
        table.schema_.columnMetas.front().integrities |= (1 | 2 | 4);
        table.constraintsByColumn_[table.schema_.columns.front()].notNull = true;
        table.constraintsByColumn_[table.schema_.columns.front()].unique = true;
    }
    return table;
}

void Table::insert(const std::vector<std::string>& values) {
    const std::vector<std::string> normalized = normalizeInputValues(values);

    const std::string primaryKey = makePrimaryKey(normalized);
    ensure(!primaryKey.empty(), "primary key (first column) cannot be empty");
    ensure(!containsPrimaryKey(primaryKey), "duplicate primary key: " + primaryKey);
    enforceRowConstraints(normalized, nullptr);

    Row row{normalized};
    index_.insert(primaryKey, row);
    TupleRef ref = dataPages_.allocate(normalized);
    std::uint64_t packed = ref.pack();
    primaryKeyOffsets_[primaryKey] = packed;
    primaryKeyOffsetsOrdered_[primaryKey] = packed;

    for (std::size_t i = 1; i < schema_.columns.size(); ++i) {
        auto it = secondaryIndexes_.find(schema_.columns[i]);
        if (it != secondaryIndexes_.end() && it->second.active) {
            it->second.add(normalized[i], packed);
            it->second.save(it->second.filePath);
        }
    }

    syncIndexPages();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
}

bool Table::updateByPrimaryKey(const std::string& primaryKey,
                                const std::vector<std::string>& newValues) {
    if (primaryKey.empty() || newValues.empty()) return false;
    const std::vector<std::string> normalized = normalizeInputValues(newValues);

    auto pkIt = primaryKeyOffsets_.find(primaryKey);
    if (pkIt == primaryKeyOffsets_.end()) return false;
    TupleRef oldRef = TupleRef::unpack(pkIt->second);

    Row oldRow;
    if (!dataPages_.read(oldRef, oldRow)) return false;
    if (oldRow.values.empty() || oldRow.values.front() != primaryKey) return false;

    const std::string newPrimaryKey = makePrimaryKey(normalized);
    if (newPrimaryKey.empty()) return false;
    if (newPrimaryKey != primaryKey && containsPrimaryKey(newPrimaryKey)) return false;
    enforceRowConstraints(normalized, &primaryKey);

    for (std::size_t i = 1; i < schema_.columns.size() && i < oldRow.values.size(); ++i) {
        auto it = secondaryIndexes_.find(schema_.columns[i]);
        if (it != secondaryIndexes_.end() && it->second.active) {
            it->second.remove(oldRow.values[i], pkIt->second);
        }
    }

    dataPages_.markDeleted(oldRef);
    TupleRef newRef = dataPages_.allocate(normalized);
    std::uint64_t packed = newRef.pack();

    primaryKeyOffsets_.erase(primaryKey);
    primaryKeyOffsetsOrdered_.erase(primaryKey);
    primaryKeyOffsets_[newPrimaryKey] = packed;
    primaryKeyOffsetsOrdered_[newPrimaryKey] = packed;

    if (newPrimaryKey != primaryKey) index_.remove(primaryKey);
    index_.insert(newPrimaryKey, Row{{newPrimaryKey}});

    for (std::size_t i = 1; i < schema_.columns.size() && i < normalized.size(); ++i) {
        auto it = secondaryIndexes_.find(schema_.columns[i]);
        if (it != secondaryIndexes_.end() && it->second.active) {
            it->second.add(normalized[i], packed);
            it->second.save(it->second.filePath);
        }
    }

    syncIndexPages();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::deleteByPrimaryKey(const std::string& primaryKey) {
    if (primaryKey.empty()) return false;

    auto pkIt = primaryKeyOffsets_.find(primaryKey);
    if (pkIt == primaryKeyOffsets_.end()) return false;
    TupleRef oldRef = TupleRef::unpack(pkIt->second);

    Row oldRow;
    if (!dataPages_.read(oldRef, oldRow)) return false;

    for (std::size_t i = 1; i < schema_.columns.size() && i < oldRow.values.size(); ++i) {
        auto it = secondaryIndexes_.find(schema_.columns[i]);
        if (it != secondaryIndexes_.end() && it->second.active) {
            it->second.remove(oldRow.values[i], pkIt->second);
            it->second.save(it->second.filePath);
        }
    }

    dataPages_.markDeleted(oldRef);
    primaryKeyOffsets_.erase(primaryKey);
    primaryKeyOffsetsOrdered_.erase(primaryKey);
    index_.remove(primaryKey);
    syncIndexPages();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

std::size_t Table::updateByCondition(const std::vector<WhereCondition>& whereConditions,
                                      const std::vector<std::string>& newValues) {
    if (whereConditions.empty()) return 0;
    std::vector<Row> matched = select({"*"}, whereConditions);
    std::size_t count = 0;
    for (const auto& row : matched) {
        if (row.values.empty()) continue;
        if (updateByPrimaryKey(row.values.front(), newValues)) ++count;
    }
    return count;
}

std::size_t Table::deleteByCondition(const std::vector<WhereCondition>& whereConditions) {
    if (whereConditions.empty()) return 0;
    std::vector<Row> matched = select({"*"}, whereConditions);
    std::size_t count = 0;
    for (const auto& row : matched) {
        if (row.values.empty()) continue;
        if (deleteByPrimaryKey(row.values.front())) ++count;
    }
    return count;
}

void Table::truncate() {
    dataPages_.scan([&](TupleRef ref, const Row&) { dataPages_.markDeleted(ref); });
    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();
    std::filesystem::resize_file(dataFilePath(), 0);
    syncIndexPages();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
}

Table::SubqueryResult Table::evaluateSubquery(const SubquerySpec& spec) const {
    SubqueryResult result;
    if (spec.dbName.empty() || spec.tableName.empty()) return result;

    auto dbPath = dbPath_.parent_path() / spec.dbName;
    if (spec.dbName == dbPath_.filename().string()) dbPath = dbPath_;
    else if (!std::filesystem::exists(dbPath)) return result;

    auto targetTable = Table::load(dbPath, spec.tableName);

    if (!spec.aggregates.empty()) {
        auto agg = targetTable.aggregate(spec.aggregates, spec.whereConditions);
        result.kind = SubqueryKind::Scalar;
        if (!agg.empty()) result.scalarValue = agg.front();
        return result;
    }

    auto rows = targetTable.select(spec.targetColumns.empty() ? std::vector<std::string>{"*"} : spec.targetColumns,
                                    spec.whereConditions, {}, spec.options);
    result.kind = SubqueryKind::RowSet;
    result.rows.reserve(rows.size());
    for (const auto& r : rows) {
        if (!r.values.empty()) result.rows.push_back(r.values.front());
    }
    return result;
}

Table::SubqueryResult Table::evaluateSubqueryForRow(const SubquerySpec& spec, const Row& outerRow) const {
    SubquerySpec resolved = spec;
    for (auto& cond : resolved.whereConditions) {
        if (cond.value.size() > 7 && cond.value.rfind("$outer.", 0) == 0) {
            std::string colName = cond.value.substr(7);
            for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
                if (schema_.columns[i] == colName && i < outerRow.values.size()) {
                    cond.value = outerRow.values[i];
                    break;
                }
            }
        }
        if (cond.secondValue.size() > 7 && cond.secondValue.rfind("$outer.", 0) == 0) {
            std::string colName = cond.secondValue.substr(7);
            for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
                if (schema_.columns[i] == colName && i < outerRow.values.size()) {
                    cond.secondValue = outerRow.values[i];
                    break;
                }
            }
        }
    }
    auto dbPath = dbPath_.parent_path() / resolved.dbName;
    if (resolved.dbName == dbPath_.filename().string()) dbPath = dbPath_;
    else if (!std::filesystem::exists(dbPath)) return SubqueryResult{};
    auto targetTable = Table::load(dbPath, resolved.tableName);
    return targetTable.evaluateSubquery(resolved);
}

std::size_t Table::compact() {
    std::size_t removed = dataPages_.compactAll();
    if (removed > 0) {
        index_.clear();
        primaryKeyOffsets_.clear();
        primaryKeyOffsetsOrdered_.clear();
        dataPages_.scan([&](TupleRef ref, const Row& row) {
            if (!row.values.empty()) {
                std::string pk = row.values.front();
                primaryKeyOffsets_[pk] = ref.pack();
                primaryKeyOffsetsOrdered_[pk] = ref.pack();
                index_.insert(pk, Row{{pk}});
            }
        });
        syncIndexPages();
        flushMeta();
        TableVersionManager::incrementVersion(dbPath_, schema_.name);
    }
    return removed;
}

bool Table::addColumn(const std::string& name, DataType type, std::uint16_t varcharLen,
                       const std::string& defaultValue) {
    ensure(!name.empty(), "column name cannot be empty");
    for (const auto& col : schema_.columns) ensure(col != name, "column already exists: " + name);

    std::vector<Row> oldRows = dataPages_.scanAll();
    std::vector<TupleRef> oldRefs;
    dataPages_.scan([&](TupleRef ref, const Row&) { oldRefs.push_back(ref); });

    for (auto ref : oldRefs) dataPages_.markDeleted(ref);

    schema_.columns.push_back(name);
    ColumnMeta meta;
    meta.dataType = type; meta.varcharLen = varcharLen; meta.defaultValue = defaultValue;
    schema_.columnMetas.push_back(meta);

    for (const auto& row : oldRows) {
        std::vector<std::string> vals = row.values;
        vals.push_back(defaultValue);
        dataPages_.allocate(vals);
    }

    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();
    dataPages_.scan([&](TupleRef ref, const Row& row) {
        if (!row.values.empty()) {
            std::string pk = row.values.front();
            index_.insert(pk, Row{{pk}});
            uint64_t p = ref.pack();
            primaryKeyOffsets_[pk] = p; primaryKeyOffsetsOrdered_[pk] = p;
        }
    });

    flushMeta();
    flushIntegrityMeta();
    syncIndexPages();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::rename(const std::string& newName) {
    ensure(!newName.empty(), "new table name cannot be empty");
    std::string oldName = schema_.name;
    if (oldName == newName) return true;

    auto renameFile = [&](const std::string& ext) {
        auto oldP = dbPath_ / (oldName + ext);
        auto newP = dbPath_ / (newName + ext);
        if (std::filesystem::exists(oldP)) std::filesystem::rename(oldP, newP);
    };
    renameFile(".tdf"); renameFile(".trd"); renameFile(".tic"); renameFile(".tid"); renameFile(".ver");
    for (const auto& col : schema_.columns) {
        auto o = dbPath_ / (oldName + "." + col + ".nidx");
        auto n = dbPath_ / (newName + "." + col + ".nidx");
        if (std::filesystem::exists(o)) std::filesystem::rename(o, n);
    }
    schema_.name = newName;
    flushMeta();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::dropConstraint(const std::string& column, ConstraintType type) {
    auto it = constraintsByColumn_.find(column);
    if (it == constraintsByColumn_.end()) return false;
    auto& spec = it->second;
    if (type == ConstraintType::NOT_NULL) spec.notNull = false;
    else if (type == ConstraintType::UNIQUE) spec.unique = false;
    else if (type == ConstraintType::DEFAULT_VALUE) { spec.hasDefault = false; spec.defaultValue.clear(); }
    else if (type == ConstraintType::CHECK_CONSTRAINT) { spec.hasCheck = false; spec.checkExpr.clear(); }
    else return false;

    const std::size_t idx = columnIndex(column);
    if (idx < schema_.columnMetas.size()) {
        if (type == ConstraintType::NOT_NULL) schema_.columnMetas[idx].integrities &= ~1;
        else if (type == ConstraintType::UNIQUE) schema_.columnMetas[idx].integrities &= ~4;
    }
    flushIntegrityMeta();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::dropColumn(const std::string& name) {
    ensure(schema_.columns.size() > 1, "cannot drop the only column");
    ensure(name != schema_.columns.front(), "cannot drop primary key column");

    std::size_t dropIdx = columnIndex(name);
    std::vector<Row> oldRows = dataPages_.scanAll();
    std::vector<TupleRef> oldRefs;
    dataPages_.scan([&](TupleRef ref, const Row&) { oldRefs.push_back(ref); });
    for (auto ref : oldRefs) dataPages_.markDeleted(ref);

    schema_.columns.erase(schema_.columns.begin() + static_cast<std::ptrdiff_t>(dropIdx));
    schema_.columnMetas.erase(schema_.columnMetas.begin() + static_cast<std::ptrdiff_t>(dropIdx));

    for (const auto& row : oldRows) {
        std::vector<std::string> vals;
        for (std::size_t i = 0; i < row.values.size(); ++i)
            if (i != dropIdx) vals.push_back(row.values[i]);
        dataPages_.allocate(vals);
    }

    // Remove secondary index
    auto si = secondaryIndexes_.find(name);
    if (si != secondaryIndexes_.end()) {
        std::filesystem::remove(si->second.filePath);
        secondaryIndexes_.erase(si);
    }

    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();
    dataPages_.scan([&](TupleRef ref, const Row& row) {
        if (!row.values.empty()) {
            std::string pk = row.values.front();
            index_.insert(pk, Row{{pk}});
            uint64_t p = ref.pack();
            primaryKeyOffsets_[pk] = p; primaryKeyOffsetsOrdered_[pk] = p;
        }
    });

    flushMeta();
    flushIntegrityMeta();
    syncIndexPages();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::renameColumn(const std::string& oldName, const std::string& newName) {
    ensure(!newName.empty(), "new column name cannot be empty");
    for (const auto& c : schema_.columns) ensure(c != newName || c == oldName, "column already exists: " + newName);
    std::size_t idx = columnIndex(oldName);
    schema_.columns[idx] = newName;
    if (idx < schema_.columnMetas.size() && idx == 0) {
        // Primary key rename: nothing special needed since first column is always PK
    }
    // Rename .nidx file if exists
    auto oldNidx = nonPrimaryIndexFilePath(oldName);
    if (std::filesystem::exists(oldNidx)) {
        std::filesystem::rename(oldNidx, nonPrimaryIndexFilePath(newName));
    }
    flushMeta();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::alterColumnType(const std::string& column, DataType newType, std::uint16_t varcharLen) {
    std::size_t idx = columnIndex(column);
    if (idx >= schema_.columnMetas.size()) return false;
    DataType oldType = schema_.columnMetas[idx].dataType;
    if (oldType == newType && (newType != DataType::VARCHAR || schema_.columnMetas[idx].varcharLen == varcharLen))
        return true;

    // Validate existing data for narrowing conversions
    if (newType == DataType::INT || newType == DataType::FLOAT) {
        auto rows = dataPages_.scanAll();
        for (const auto& row : rows) {
            if (idx >= row.values.size()) continue;
            double v = 0; errno = 0;
            char* e = nullptr;
            v = std::strtod(row.values[idx].c_str(), &e);
            bool ok = (e != row.values[idx].c_str() && *e == '\0' && errno != ERANGE);
            if (!ok) { ensure(false, "value '" + row.values[idx] + "' cannot convert to numeric type"); }
            if (newType == DataType::INT && v != static_cast<double>(static_cast<std::int64_t>(v)))
                ensure(false, "value '" + row.values[idx] + "' cannot convert to INT");
        }
    }
    if (newType == DataType::VARCHAR && varcharLen > 0) {
        auto rows = dataPages_.scanAll();
        for (const auto& row : rows) {
            if (idx < row.values.size() && row.values[idx].size() > varcharLen)
                ensure(false, "value exceeds VARCHAR(" + std::to_string(varcharLen) + ")");
        }
    }

    schema_.columnMetas[idx].dataType = newType;
    schema_.columnMetas[idx].varcharLen = varcharLen;
    flushMeta();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

std::vector<Row> Table::select(const std::vector<std::string>& targetColumns,
                               const std::vector<WhereCondition>& whereConditions,
                               const SelectOptions& options) const {
    return select(targetColumns, whereConditions, {}, options);
}

std::vector<Row> Table::select(const std::vector<std::string>& targetColumns,
                               const std::vector<WhereCondition>& whereConditions,
                               const std::vector<QueryConstraint>& queryConstraints,
                               const SelectOptions& options) const {
    std::shared_ptr<ConditionNode> whereTree;
    if (!whereConditions.empty()) {
        whereTree = std::make_shared<ConditionNode>();
        whereTree->isLeaf = true;
        whereTree->condition = whereConditions.front();
        for (std::size_t i = 1; i < whereConditions.size(); ++i) {
            auto rightLeaf = std::make_shared<ConditionNode>();
            rightLeaf->isLeaf = true;
            rightLeaf->condition = whereConditions[i];

            auto parent = std::make_shared<ConditionNode>();
            parent->isLeaf = false;
            parent->logicalOp = LogicalOp::AND;
            parent->left = whereTree;
            parent->right = rightLeaf;
            whereTree = parent;
        }
    }
    return select(targetColumns, whereTree, queryConstraints, options);
}

std::vector<Row> Table::select(const std::vector<std::string>& targetColumns,
                               const std::shared_ptr<ConditionNode>& whereTree,
                               const SelectOptions& options) const {
    return select(targetColumns, whereTree, {}, options);
}

std::vector<Row> Table::select(const std::vector<std::string>& targetColumns,
                               const std::shared_ptr<ConditionNode>& whereTree,
                               const std::vector<QueryConstraint>& queryConstraints,
                               const SelectOptions& options) const {
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

    std::vector<Row> matchedRows;
    const IndexCandidateResult indexCandidates = collectIndexCandidates(whereTree);
    if (indexCandidates.constrained) {
        for (const auto offset : indexCandidates.offsets) {
            Row row;
            if (!readRowByOffset(offset, row)) {
                continue;
            }
            if (row.values.size() != schema_.columns.size()) {
                continue;
            }
            if (!matchConditionTree(row, whereTree)) {
                continue;
            }
            matchedRows.push_back(std::move(row));
        }
    } else {
        dataPages_.scan([&](TupleRef, const Row& row) {
            if (row.values.size() != schema_.columns.size()) return;
            if (matchConditionTree(row, whereTree))
                matchedRows.push_back(row);
        });
    }

    if (!queryConstraints.empty()) {
        const auto uniqueCounters = buildUniqueCountersForQuery(queryConstraints);
        std::vector<Row> constrainedRows;
        constrainedRows.reserve(matchedRows.size());
        for (const auto& row : matchedRows) {
            if (matchQueryConstraints(row, queryConstraints, uniqueCounters)) {
                constrainedRows.push_back(row);
            }
        }
        matchedRows = std::move(constrainedRows);
    }

    if (!options.orderByColumn.empty()) {
        const std::size_t orderIndex = columnIndex(options.orderByColumn);
        const bool desc = options.orderByDesc;
        std::stable_sort(matchedRows.begin(),
                         matchedRows.end(),
                         [orderIndex, desc](const Row& lhs, const Row& rhs) {
                             const std::string& left = lhs.values[orderIndex];
                             const std::string& right = rhs.values[orderIndex];
                             double leftNum = 0.0;
                             double rightNum = 0.0;
                             const bool leftIsNum = tryParseNumber(left, leftNum);
                             const bool rightIsNum = tryParseNumber(right, rightNum);
                             if (leftIsNum && rightIsNum) {
                                 return desc ? (leftNum > rightNum) : (leftNum < rightNum);
                             }
                             return desc ? (left > right) : (left < right);
                         });
    }

    if (options.hasLimit && options.limit < matchedRows.size()) {
        matchedRows.resize(options.limit);
    }

    std::vector<Row> result;
    result.reserve(matchedRows.size());
    for (const auto& row : matchedRows) {
        Row projected;
        projected.values.reserve(projectedIndexes.size());
        for (const auto index : projectedIndexes) {
            projected.values.push_back(row.values[index]);
        }
        result.push_back(std::move(projected));
    }
    return result;
}

std::vector<std::string> Table::aggregate(const std::vector<AggregateExpr>& expressions,
                                          const std::vector<WhereCondition>& whereConditions) const {
    std::shared_ptr<ConditionNode> whereTree;
    if (!whereConditions.empty()) {
        whereTree = std::make_shared<ConditionNode>();
        whereTree->isLeaf = true;
        whereTree->condition = whereConditions.front();
        for (std::size_t i = 1; i < whereConditions.size(); ++i) {
            auto rightLeaf = std::make_shared<ConditionNode>();
            rightLeaf->isLeaf = true;
            rightLeaf->condition = whereConditions[i];

            auto parent = std::make_shared<ConditionNode>();
            parent->isLeaf = false;
            parent->logicalOp = LogicalOp::AND;
            parent->left = whereTree;
            parent->right = rightLeaf;
            whereTree = parent;
        }
    }
    return aggregate(expressions, whereTree);
}

std::vector<std::string> Table::aggregate(const std::vector<AggregateExpr>& expressions,
                                          const std::shared_ptr<ConditionNode>& whereTree) const {
    ensure(!expressions.empty(), "aggregate expressions cannot be empty");
    std::vector<Row> rows;
    dataPages_.scan([&](TupleRef, const Row& row) {
        if (row.values.size() != schema_.columns.size()) return;
        if (matchConditionTree(row, whereTree))
            rows.push_back(row);
    });

    std::vector<std::string> out;
    out.reserve(expressions.size());
    for (const auto& expr : expressions) {
        if (expr.op == AggregateOp::COUNT) {
            if (expr.column.empty() || expr.column == "*") {
                out.push_back(std::to_string(rows.size()));
            } else {
                const std::size_t idx = columnIndex(expr.column);
                std::size_t count = 0;
                for (const auto& row : rows) {
                    if (idx < row.values.size() && !row.values[idx].empty()) {
                        ++count;
                    }
                }
                out.push_back(std::to_string(count));
            }
            continue;
        }

        const std::size_t idx = columnIndex(expr.column);
        if (expr.op == AggregateOp::SUM || expr.op == AggregateOp::AVG) {
            double sum = 0.0;
            std::size_t numericCount = 0;
            for (const auto& row : rows) {
                if (idx >= row.values.size()) {
                    continue;
                }
                double num = 0.0;
                if (!tryParseNumber(row.values[idx], num)) {
                    continue;
                }
                sum += num;
                ++numericCount;
            }
            if (expr.op == AggregateOp::SUM) {
                out.push_back(formatDouble(sum));
            } else {
                out.push_back(numericCount == 0 ? "0" : formatDouble(sum / static_cast<double>(numericCount)));
            }
            continue;
        }

        bool hasValue = false;
        std::string best;
        for (const auto& row : rows) {
            if (idx >= row.values.size()) {
                continue;
            }
            const std::string& value = row.values[idx];
            if (!hasValue) {
                best = value;
                hasValue = true;
                continue;
            }
            if (expr.op == AggregateOp::MIN) {
                if (compareValue(value, CompareOp::LT, best)) {
                    best = value;
                }
            } else if (expr.op == AggregateOp::MAX) {
                if (compareValue(value, CompareOp::GT, best)) {
                    best = value;
                }
            }
        }
        out.push_back(hasValue ? best : "");
    }
    return out;
}

ColumnMeta Table::toColumnMeta(const ColumnDefinition& def) {
    ColumnMeta meta;
    meta.dataType = def.dataType;
    meta.varcharLen = def.varcharLen;
    if (def.constraints.notNull)  meta.integrities |= 1;
    if (def.constraints.unique)   meta.integrities |= 4;
    if (def.constraints.hasDefault) meta.defaultValue = def.constraints.defaultValue;
    return meta;
}

bool Table::addColumnConstraint(const ColumnConstraintSpec& spec) {
    ensure(!spec.column.empty(), "constraint column cannot be empty");
    (void)columnIndex(spec.column);
    ColumnConstraintSpec merged = constraintsByColumn_[spec.column];
    merged.column = spec.column;
    merged.notNull = merged.notNull || spec.notNull;
    merged.unique = merged.unique || spec.unique;
    if (spec.hasDefault) {
        merged.hasDefault = true;
        merged.defaultValue = spec.defaultValue;
    }
    if (spec.hasCheck) {
        merged.hasCheck = true;
        merged.checkExpr = spec.checkExpr;
    }
    ensure(validateConstraintForExistingRows(merged), "constraint conflicts with existing rows");
    constraintsByColumn_[spec.column] = merged;
    const std::size_t idx = columnIndex(spec.column);
    if (idx >= schema_.columnMetas.size()) {
        schema_.columnMetas.resize(schema_.columns.size());
    }
    if (merged.notNull) {
        schema_.columnMetas[idx].integrities |= 1;
    }
    if (merged.unique) {
        schema_.columnMetas[idx].integrities |= 4;
    }
    if (merged.hasDefault) {
        schema_.columnMetas[idx].defaultValue = merged.defaultValue;
    }
    flushMeta();
    flushIntegrityMeta();
    TableVersionManager::incrementVersion(dbPath_, schema_.name);
    return true;
}

bool Table::addColumnConstraints(const std::vector<ColumnConstraintSpec>& specs) {
    auto backup = constraintsByColumn_;
    auto metasBackup = schema_.columnMetas;
    try {
        for (const auto& spec : specs) {
            addColumnConstraint(spec);
        }
        return true;
    } catch (...) {
        constraintsByColumn_ = std::move(backup);
        schema_.columnMetas = std::move(metasBackup);
        flushMeta();
        flushIntegrityMeta();
        return false;
    }
}

std::vector<Table::ColumnConstraintSpec> Table::getColumnConstraints() const {
    std::vector<ColumnConstraintSpec> out;
    out.reserve(constraintsByColumn_.size());
    for (const auto& col : schema_.columns) {
        auto it = constraintsByColumn_.find(col);
        if (it != constraintsByColumn_.end()) {
            out.push_back(it->second);
        }
    }
    return out;
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

std::filesystem::path Table::nonPrimaryIndexFilePath(const std::string& column) const {
    return dbPath_ / (schema_.name + "." + column + ".nidx");
}

std::filesystem::path Table::versionFilePath() const {
    return dbPath_ / (schema_.name + ".ver");
}

std::uint64_t Table::getVersion() const {
    return TableVersionManager::getVersion(dbPath_, schema_.name);
}

void Table::flushMeta() const {
    std::ofstream ofs(metaFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to write table meta file: " + metaFilePath().string());

    ofs << "schema_version=2\n";
    ofs << "table=" << schema_.name << '\n';
    std::vector<std::string> columnMetas;
    columnMetas.reserve(schema_.columns.size());
    for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
        std::string typeStr;
        switch (schema_.columnMetas[i].dataType) {
            case DataType::INT:     typeStr = "INT"; break;
            case DataType::FLOAT:   typeStr = "FLOAT"; break;
            case DataType::VARCHAR: typeStr = "VARCHAR(" + std::to_string(schema_.columnMetas[i].varcharLen) + ")"; break;
            default:                typeStr = "TEXT"; break;
        }
        columnMetas.push_back(schema_.columns[i] + ":" + typeStr);
    }
    ofs << "columns=" << join(columnMetas, "|") << '\n';
    ofs << "primary_key=" << schema_.columns.front() << '\n';
    IndexBlock primaryIdx;
    primaryIdx.setName(stringToArray<128>("PRIMARY"));
    {
        std::array<std::array<char, 128>, 2> fds{};
        fds[0] = stringToArray<128>(schema_.columns.front());
        primaryIdx.setFields(fds);
    }
    primaryIdx.setIndexFile(stringToArray<256>(schema_.name + ".tid"));
    ofs << primaryIdx.toDescriptorLine("index_definitions") << '\n';
    for (std::size_t i = 1; i < schema_.columns.size(); ++i) {
        IndexBlock secIdx;
        secIdx.setName(stringToArray<128>(schema_.columns[i]));
        {
            std::array<std::array<char, 128>, 2> fds{};
            fds[0] = stringToArray<128>(schema_.columns[i]);
            secIdx.setFields(fds);
        }
        secIdx.setIndexFile(stringToArray<256>(schema_.name + "." + schema_.columns[i] + ".nidx"));
        ofs << secIdx.toDescriptorLine("index_reserved") << '\n';
    }

    std::vector<std::string> integList;
    integList.reserve(schema_.columnMetas.size());
    for (const auto& meta : schema_.columnMetas) {
        integList.push_back(std::to_string(meta.integrities));
    }
    ofs << "integrities=" << join(integList, "|") << '\n';

    std::vector<std::string> defaultList;
    defaultList.reserve(schema_.columnMetas.size());
    for (const auto& meta : schema_.columnMetas) {
        defaultList.push_back(meta.defaultValue);
    }
    ofs << "defaults=" << join(defaultList, "|") << '\n';

    for (const auto& [col, nextVal] : autoIncCounters_) {
        ofs << "autoInc:" << col << "=" << nextVal << '\n';
    }
}

void Table::flushIntegrityMeta() const {
    std::ofstream ofs(integrityFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to write table integrity file: " + integrityFilePath().string());

    ofs << "constraints_version=1\n";

    IntegrityBlock pkBlock;
    pkBlock.setType(IntegrityBlock::TYPE_PRIMARY_KEY);
    pkBlock.setField(stringToArray<128>(schema_.columns.front()));
    ofs << pkBlock.toDescriptorLine() << '\n';

    IndexBlock primaryIdx;
    primaryIdx.setName(stringToArray<128>("PRIMARY"));
    primaryIdx.setIndexFile(stringToArray<256>(schema_.name + ".tid"));
    ofs << primaryIdx.toDescriptorLine("index") << '\n';

    for (std::size_t i = 1; i < schema_.columns.size(); ++i) {
        IndexBlock secIdx;
        secIdx.setName(stringToArray<128>(schema_.columns[i]));
        {
            std::array<std::array<char, 128>, 2> fds{};
            fds[0] = stringToArray<128>(schema_.columns[i]);
            secIdx.setFields(fds);
        }
        secIdx.setIndexFile(stringToArray<256>(schema_.name + "." + schema_.columns[i] + ".nidx"));
        ofs << secIdx.toDescriptorLine("index_reserved", "") << '\n';
    }
    for (const auto& col : schema_.columns) {
        const auto it = constraintsByColumn_.find(col);
        if (it == constraintsByColumn_.end()) {
            continue;
        }
        const auto& spec = it->second;
        if (spec.notNull) {
            IntegrityBlock b;
            b.setType(IntegrityBlock::TYPE_NOT_NULL);
            b.setField(stringToArray<128>(col));
            ofs << b.toDescriptorLine() << '\n';
        }
        if (spec.unique) {
            IntegrityBlock b;
            b.setType(IntegrityBlock::TYPE_UNIQUE);
            b.setField(stringToArray<128>(col));
            ofs << b.toDescriptorLine() << '\n';
        }
        if (spec.hasDefault) {
            IntegrityBlock b;
            b.setType(IntegrityBlock::TYPE_DEFAULT);
            b.setField(stringToArray<128>(col));
            b.setParam(stringToArray<256>(encodeConstraintValue(spec.defaultValue)));
            ofs << b.toDescriptorLine() << '\n';
        }
        if (spec.hasCheck) {
            IntegrityBlock b;
            b.setType(IntegrityBlock::TYPE_CHECK);
            b.setField(stringToArray<128>(col));
            b.setParam(stringToArray<256>(encodeConstraintValue(spec.checkExpr)));
            ofs << b.toDescriptorLine() << '\n';
        }
    }
}

void Table::loadConstraintsFromIntegrityMeta() {
    std::ifstream ifs(integrityFilePath());
    if (!ifs.good()) {
        return;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        IntegrityBlock block;
        if (!IntegrityBlock::fromDescriptorLine(line, block)) {
            continue;
        }
        const std::string fieldStr = arrayToString(block.getField());
        if (fieldStr.empty()) continue;
        auto& spec = constraintsByColumn_[fieldStr];
        spec.column = fieldStr;
        switch (block.getType()) {
            case IntegrityBlock::TYPE_NOT_NULL:
                spec.notNull = true;
                break;
            case IntegrityBlock::TYPE_UNIQUE:
                spec.unique = true;
                break;
            case IntegrityBlock::TYPE_DEFAULT:
                spec.hasDefault = true;
                spec.defaultValue = decodeConstraintValue(arrayToString(block.getParam()));
                break;
            case IntegrityBlock::TYPE_CHECK:
                spec.hasCheck = true;
                spec.checkExpr = decodeConstraintValue(arrayToString(block.getParam()));
                break;
            default:
                break;
        }
    }
    if (schema_.columnMetas.size() < schema_.columns.size()) {
        schema_.columnMetas.resize(schema_.columns.size());
    }
    for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
        const auto it = constraintsByColumn_.find(schema_.columns[i]);
        if (it == constraintsByColumn_.end()) {
            continue;
        }
        const auto& spec = it->second;
        if (spec.notNull) {
            schema_.columnMetas[i].integrities |= 1;
        }
        if (spec.unique) {
            schema_.columnMetas[i].integrities |= 4;
        }
        if (spec.hasDefault) {
            schema_.columnMetas[i].defaultValue = spec.defaultValue;
        }
    }
    if (!schema_.columns.empty()) {
        constraintsByColumn_[schema_.columns.front()].column = schema_.columns.front();
        constraintsByColumn_[schema_.columns.front()].notNull = true;
        constraintsByColumn_[schema_.columns.front()].unique = true;
        schema_.columnMetas.front().integrities |= (1 | 2 | 4);
    }
}

void Table::ColumnIndex::save(const std::filesystem::path& path) {
    std::ofstream ofs(path, std::ios::trunc);
    for (const auto& kv : entries) {
        ofs << kv.first << "|" << kv.second << '\n';
    }
}

bool Table::readRowByOffset(std::uint64_t packed, Row& row) const {
    return dataPages_.read(TupleRef::unpack(packed), row);
}

std::vector<Row> Table::readAllDataRows() const {
    return dataPages_.scanAll();
}

void Table::ColumnIndex::load(const std::filesystem::path& path) {
    entries.clear();
    if (!std::filesystem::exists(path)) return;
    std::ifstream ifs(path);
    if (!ifs.good()) return;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        auto sep = line.rfind('|');
        if (sep == std::string::npos || sep == 0 || sep == line.size() - 1) continue;
        try {
            std::string key = line.substr(0, sep);
            std::uint64_t off = std::stoull(line.substr(sep + 1));
            entries.emplace(std::move(key), off);
        } catch (...) {}
    }
}

bool Table::ColumnIndex::lookup(const std::string& value, Table::CompareOp op,
                                 const std::string& secondValue,
                                 const std::vector<std::string>& values,
                                 std::vector<std::uint64_t>& offsets) const {
    offsets.clear();
    switch (op) {
        case Table::CompareOp::EQ: {
            auto range = entries.equal_range(value);
            for (auto it = range.first; it != range.second; ++it) offsets.push_back(it->second);
            break;
        }
        case Table::CompareOp::NE: {
            for (const auto& kv : entries) {
                if (kv.first != value) offsets.push_back(kv.second);
            }
            break;
        }
        case Table::CompareOp::GT: {
            for (auto it = entries.upper_bound(value); it != entries.end(); ++it)
                offsets.push_back(it->second);
            break;
        }
        case Table::CompareOp::GE: {
            for (auto it = entries.lower_bound(value); it != entries.end(); ++it)
                offsets.push_back(it->second);
            break;
        }
        case Table::CompareOp::LT: {
            for (auto it = entries.begin(); it != entries.lower_bound(value); ++it)
                offsets.push_back(it->second);
            break;
        }
        case Table::CompareOp::LE: {
            for (auto it = entries.begin(); it != entries.upper_bound(value); ++it)
                offsets.push_back(it->second);
            break;
        }
        case Table::CompareOp::BETWEEN: {
            for (auto it = entries.lower_bound(value);
                 it != entries.upper_bound(secondValue); ++it)
                offsets.push_back(it->second);
            break;
        }
        case Table::CompareOp::IN: {
            for (const auto& v : values) {
                auto range = entries.equal_range(v);
                for (auto it = range.first; it != range.second; ++it) offsets.push_back(it->second);
            }
            break;
        }
        case Table::CompareOp::LIKE:
            return false;
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return true;
}

TupleRef Table::DataPageManager::allocate(const std::vector<std::string>& values) {
    const auto path = table.dataFilePath();
    Row row{values};
    std::string tupleData = serializeRow(row);
    std::uintmax_t fileSize = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
    std::uint32_t maxPg = static_cast<std::uint32_t>(fileSize / kDataPageSize);

    for (std::uint32_t pg = 1; pg <= maxPg; ++pg) {
        std::string pageData = FileManager::readPage(path, pg);
        if (!pageData.empty()) {
            // 确保 pageData 有完整的页缓冲区，避免越界写入
            // 作者：NAPH130
            if (pageData.size() < kDataPageSize) {
                pageData.resize(kDataPageSize, '\0');
            }
            DataPageHeader hdr;
            std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
            if (hdr.freeEnd - hdr.freeStart >= tupleData.size() + 1 + kSlotSize) {
                // Write to existing page
                std::string full = tupleData + '\n';
                std::memcpy(pageData.data() + hdr.freeStart, full.data(), full.size());
                PageSlot slot; slot.offset = hdr.freeStart; slot.flags = 0;
                std::memcpy(pageData.data() + hdr.freeEnd - kSlotSize, &slot, sizeof(PageSlot));
                hdr.freeStart += static_cast<std::uint16_t>(full.size());
                TupleRef ref; ref.pageId = pg; ref.slotIndex = hdr.slotCount;
                hdr.freeEnd -= kSlotSize; hdr.slotCount++;
                std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
                FileManager::writePage(path, pg, pageData);
                return ref;
            }
        }
    }
    // Allocate new page
    std::uint32_t pg = maxPg + 1;
    std::string pageData(kDataPageSize, '\0');
    DataPageHeader hdr;
    hdr.pageId = pg; hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
    std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
    std::string full = tupleData + '\n';
    std::memcpy(pageData.data() + hdr.freeStart, full.data(), full.size());
    PageSlot slot; slot.offset = hdr.freeStart; slot.flags = 0;
    std::memcpy(pageData.data() + hdr.freeEnd - kSlotSize, &slot, sizeof(PageSlot));
    hdr.freeStart += static_cast<std::uint16_t>(full.size());
    hdr.freeEnd -= kSlotSize; hdr.slotCount++;
    std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
    FileManager::writePage(path, pg, pageData);
    return {pg, static_cast<std::uint32_t>(hdr.slotCount - 1)};
}

bool Table::DataPageManager::read(TupleRef ref, Row& out) const {
    std::string pageData = FileManager::readPage(table.dataFilePath(), ref.pageId);
    if (pageData.empty()) return false;
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    if (ref.slotIndex >= hdr.slotCount) return false;
    std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - ref.slotIndex) * kSlotSize;
    PageSlot slot;
    std::memcpy(&slot, pageData.data() + slotPos, sizeof(PageSlot));
    if (slot.flags != 0) return false;
    const char* start = pageData.data() + slot.offset;
    const char* end = static_cast<const char*>(std::memchr(start, '\n', pageData.size() - slot.offset));
    if (!end) return false;
    out = deserializeRow(std::string(start, static_cast<std::size_t>(end - start)));
    return true;
}

bool Table::DataPageManager::markDeleted(TupleRef ref) {
    std::string pageData = FileManager::readPage(table.dataFilePath(), ref.pageId);
    if (pageData.empty()) return false;
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
    if (ref.slotIndex >= hdr.slotCount) return false;
    std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - ref.slotIndex) * kSlotSize;
    PageSlot slot;
    std::memcpy(&slot, pageData.data() + slotPos, sizeof(PageSlot));
    if (slot.flags != 0) return false;
    slot.flags = 1;
    std::memcpy(pageData.data() + slotPos, &slot, sizeof(PageSlot));
    return FileManager::writePage(table.dataFilePath(), ref.pageId, pageData);
}

std::vector<Row> Table::DataPageManager::scanAll() const {
    std::vector<Row> rows;
    scan([&](TupleRef, const Row& row) { rows.push_back(row); });
    return rows;
}

void Table::DataPageManager::scan(std::function<void(TupleRef, const Row&)> visitor) const {
    const auto path = table.dataFilePath();
    if (!std::filesystem::exists(path)) return;
    std::uintmax_t fileSize = std::filesystem::file_size(path);
    std::uint32_t maxPage = static_cast<std::uint32_t>(fileSize / kDataPageSize);
    for (std::uint32_t pg = 1; pg <= maxPage; ++pg) {
        std::string pageData = FileManager::readPage(path, pg);
        if (pageData.empty()) break;
        DataPageHeader hdr;
        std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
        if (hdr.pageId == 0) continue;
        for (std::uint32_t s = 0; s < hdr.slotCount; ++s) {
            std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - s) * kSlotSize;
            PageSlot slot;
            std::memcpy(&slot, pageData.data() + slotPos, sizeof(PageSlot));
            if (slot.flags != 0) continue;
            const char* start = pageData.data() + slot.offset;
            const char* end = static_cast<const char*>(std::memchr(start, '\n', pageData.size() - slot.offset));
            if (!end) continue;
            Row row = deserializeRow(std::string(start, static_cast<std::size_t>(end - start)));
            visitor(TupleRef{pg, s}, row);
        }
    }
}

bool Table::DataPageManager::compactPage(std::uint32_t pageId) {
    const auto path = table.dataFilePath();
    std::string pageData = FileManager::readPage(path, pageId);
    if (pageData.empty()) return false;
    DataPageHeader hdr;
    std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));

    // Collect active tuples
    struct ActiveSlot { std::uint32_t slotIndex; std::string data; };
    std::vector<ActiveSlot> active;
    for (std::uint32_t s = 0; s < hdr.slotCount; ++s) {
        std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - s) * kSlotSize;
        PageSlot slot;
        std::memcpy(&slot, pageData.data() + slotPos, sizeof(PageSlot));
        if (slot.flags != 0) continue;
        const char* start = pageData.data() + slot.offset;
        const char* end = static_cast<const char*>(std::memchr(start, '\n', pageData.size() - slot.offset));
        if (!end) continue;
        active.push_back({s, std::string(start, static_cast<std::size_t>(end - start) + 1)});
    }

    if (active.empty()) {
        // Page fully deleted: mark as empty, sizes shrink to 0
        std::memset(pageData.data(), 0, kDataPageSize);
        hdr.pageId = 0; hdr.slotCount = 0;
        hdr.freeStart = kDataPageHeader; hdr.freeEnd = kDataPageSize;
        std::memcpy(pageData.data(), &hdr, sizeof(DataPageHeader));
        return FileManager::writePage(path, pageId, pageData);
    }

    // Rebuild page with packed tuples
    std::string newPage(kDataPageSize, '\0');
    DataPageHeader newHdr;
    newHdr.pageId = pageId;
    newHdr.freeStart = kDataPageHeader;
    newHdr.freeEnd = kDataPageSize;
    newHdr.slotCount = hdr.slotCount;

    std::memcpy(newPage.data(), &newHdr, sizeof(DataPageHeader));

    for (const auto& as : active) {
        // Keep same slot index, update offset
        std::uint32_t newOffset = newHdr.freeStart;
        std::memcpy(newPage.data() + newOffset, as.data.data(), as.data.size());

        std::uint32_t slotPos = newHdr.freeEnd - kSlotSize;
        PageSlot slot;
        slot.offset = static_cast<std::uint16_t>(newOffset);
        slot.flags = 0;
        std::memcpy(newPage.data() + slotPos, &slot, sizeof(PageSlot));

        newHdr.freeStart += static_cast<std::uint16_t>(as.data.size());
        newHdr.freeEnd -= kSlotSize;
    }

    // Mark remaining slots as deleted (unaltered indices)
    for (std::uint32_t s = 0; s < hdr.slotCount; ++s) {
        bool isActive = false;
        for (const auto& as : active) { if (as.slotIndex == s) { isActive = true; break; } }
        if (isActive) continue;
        std::uint32_t slotPos = hdr.freeEnd + (hdr.slotCount - 1 - s) * kSlotSize;
        // Slot was already deleted, rewrite with flags=1, offset=0
        // In the new page layout, deleted slots still need entries
        // Actually, in newHdr, freeEnd has moved. The slot positions are different.
        // We should preserve all slot indices with their flags.
    }

    // Wait, this approach has a problem: we keep hdr.slotCount the same but
    // compact only active tuples. Deleted slots still need entries.
    // Simpler: just rebuild from scratch, keeping same slot indices.
    
    // Actually, the simplest correct approach:
    // For each slot 0..slotCount-1:
    //   if was active: write tuple, set slot with new offset, flags=0
    //   if was deleted: write slot with offset=0, flags=1 (no tuple)
    
    std::string rebuilt(kDataPageSize, '\0');
    DataPageHeader rhdr;
    rhdr.pageId = pageId;
    rhdr.freeStart = kDataPageHeader;
    rhdr.freeEnd = kDataPageSize;
    rhdr.slotCount = hdr.slotCount;
    
    for (std::uint32_t s = 0; s < hdr.slotCount; ++s) {
        std::uint32_t oldSlotPos = hdr.freeEnd + (hdr.slotCount - 1 - s) * kSlotSize;
        PageSlot oldSlot;
        std::memcpy(&oldSlot, pageData.data() + oldSlotPos, sizeof(PageSlot));
        
        PageSlot newSlot;
        if (oldSlot.flags == 0) {
            const char* start = pageData.data() + oldSlot.offset;
            const char* end = static_cast<const char*>(std::memchr(start, '\n', pageData.size() - oldSlot.offset));
            if (!end) { newSlot.flags = 1; newSlot.offset = 0; }
            else {
                std::size_t len = static_cast<std::size_t>(end - start) + 1;
                std::memcpy(rebuilt.data() + rhdr.freeStart, start, len);
                newSlot.offset = static_cast<std::uint16_t>(rhdr.freeStart);
                newSlot.flags = 0;
                rhdr.freeStart += static_cast<std::uint16_t>(len);
            }
        } else {
            newSlot.flags = 1;
            newSlot.offset = 0;
        }
        std::uint32_t newSlotPos = rhdr.freeEnd - kSlotSize;
        std::memcpy(rebuilt.data() + newSlotPos, &newSlot, sizeof(PageSlot));
        rhdr.freeEnd -= kSlotSize;
    }
    
    std::memcpy(rebuilt.data(), &rhdr, sizeof(DataPageHeader));
    return FileManager::writePage(path, pageId, rebuilt);
}

std::size_t Table::DataPageManager::compactAll() {
    std::size_t totalRemoved = 0;
    const auto path = table.dataFilePath();
    if (!std::filesystem::exists(path)) return 0;
    std::uintmax_t fileSize = std::filesystem::file_size(path);
    std::uint32_t maxPg = static_cast<std::uint32_t>(fileSize / kDataPageSize);
    for (std::uint32_t pg = 1; pg <= maxPg; ++pg) {
        std::string pageData = FileManager::readPage(path, pg);
        if (pageData.empty()) continue;
        DataPageHeader hdr;
        std::memcpy(&hdr, pageData.data(), sizeof(DataPageHeader));
        std::uint32_t deleted = 0;
        for (std::uint32_t s = 0; s < hdr.slotCount; ++s) {
            PageSlot slot; std::memcpy(&slot, pageData.data() + hdr.freeEnd + (hdr.slotCount-1-s)*kSlotSize, sizeof(PageSlot));
            if (slot.flags != 0) ++deleted;
        }
        if (deleted > 0) { compactPage(pg); totalRemoved += deleted; }
    }
    return totalRemoved;
}

namespace {

constexpr std::uint32_t kTidPageSize = 4096;

void padToPageSize(std::ostream& os, std::uint32_t bytesWritten) {
    const std::uint32_t remain = (bytesWritten < kTidPageSize) ? (kTidPageSize - bytesWritten) : 0;
    for (std::uint32_t i = 0; i < remain; ++i) {
        os.put('\0');
    }
}

} // namespace

void Table::syncIndexPages() {
    // 清除旧页ID后全量重写，确保增量数据被持久化
    // 作者：NAPH130
    index_.clearPageIds();

    {
        auto nodeRefs = index_.dumpNodeRefs();
        index_.clearDirtyFlags();
        nextPageId_ = 1;
        index_.assignAllPageIds(nextPageId_);

        const std::size_t nodeCount = nodeRefs.size();
        if (nodeCount == 0) {
            std::ofstream ofs(indexFilePath(), std::ios::trunc | std::ios::binary);
            ensure(ofs.good(), "failed to write table index file: " + indexFilePath().string());
            rootPageId_ = 1;
            nextPageId_ = 2;
            std::ostringstream headerOss;
            headerOss << "TID_PAGED_V3\npage_size=" << kTidPageSize << "\nroot_page=" << rootPageId_
                      << "\nnext_page=" << nextPageId_ << '\n';
            std::string header = headerOss.str();
            ofs.write(header.data(), static_cast<std::streamsize>(header.size()));
            padToPageSize(ofs, static_cast<std::uint32_t>(header.size()));
            std::ostringstream pageOss;
            pageOss << "PAGE|1|leaf=1|parent=0|prev=0|next=0|entry_count=0\nENDPAGE\n";
            std::string page = pageOss.str();
            ofs.write(page.data(), static_cast<std::streamsize>(page.size()));
            padToPageSize(ofs, static_cast<std::uint32_t>(page.size()));
            return;
        }

        std::vector<std::uint32_t> pageIds = index_.getNodePageIds();
        rootPageId_ = pageIds[0];

        std::vector<std::uint32_t> parentPageId(nodeCount, 0);
        for (std::size_t i = 0; i < nodeCount; ++i) {
            for (auto childIdx : nodeRefs[i].childIndices) {
                parentPageId[childIdx] = pageIds[i];
            }
        }

        std::vector<std::size_t> leafOrder;
        {
            struct Frame { std::size_t nodeIdx; std::size_t childCursor; };
            std::vector<Frame> stack;
            stack.push_back({0, 0});
            while (!stack.empty()) {
                auto& top = stack.back();
                const auto& ref = nodeRefs[top.nodeIdx];
                if (ref.isLeaf) { leafOrder.push_back(top.nodeIdx); stack.pop_back(); continue; }
                if (top.childCursor < ref.childIndices.size()) {
                    std::size_t childIdx = ref.childIndices[top.childCursor];
                    ++top.childCursor;
                    stack.push_back({childIdx, 0});
                } else { stack.pop_back(); }
            }
        }
        for (std::size_t i = 0; i < leafOrder.size(); ++i) {
            std::size_t ni = leafOrder[i];
            nodeRefs[ni].childIndices.clear();
            nodeRefs[ni].childIndices.push_back(i > 0 ? leafOrder[i - 1] : static_cast<std::size_t>(0));
            nodeRefs[ni].childIndices.push_back(i + 1 < leafOrder.size() ? leafOrder[i + 1] : static_cast<std::size_t>(0));
            nodeRefs[ni].isLeaf = true;
        }

        std::ofstream ofs(indexFilePath(), std::ios::trunc | std::ios::binary);
        ensure(ofs.good(), "failed to write table index file: " + indexFilePath().string());
        writeHeader(ofs);
        for (std::size_t i = 0; i < nodeCount; ++i) {
            const auto& ref = nodeRefs[i];
            const bool isLeaf = ref.isLeaf && !ref.childIndices.empty();
            std::uint32_t prevId = 0, nextId = 0;
            if (isLeaf && ref.childIndices.size() >= 2) {
                if (ref.childIndices[0] != 0) prevId = pageIds[ref.childIndices[0]];
                if (ref.childIndices[1] != 0) nextId = pageIds[ref.childIndices[1]];
            }
            std::ostringstream pageOss;
            pageOss << "PAGE|" << pageIds[i] << "|leaf=" << (isLeaf ? 1 : 0)
                    << "|parent=" << parentPageId[i] << "|prev=" << prevId << "|next=" << nextId
                    << "|entry_count=" << ref.keys.size() << '\n';
            if (isLeaf) {
                for (const auto& key : ref.keys) {
                    std::uint64_t off = 0;
                    auto it = primaryKeyOffsets_.find(key);
                    if (it != primaryKeyOffsets_.end()) off = it->second;
                    pageOss << "ENTRY|" << key << "|" << off << '\n';
                }
            } else {
                if (ref.childIndices.empty()) { pageOss << "ENDPAGE\n"; continue; }
                pageOss << "CHILD|" << pageIds[ref.childIndices[0]] << '\n';
                for (std::size_t j = 0; j < ref.keys.size(); ++j) {
                    std::uint64_t off = 0;
                    auto it = primaryKeyOffsets_.find(ref.keys[j]);
                    if (it != primaryKeyOffsets_.end()) off = it->second;
                    pageOss << "ENTRY|" << ref.keys[j] << "|" << off << '\n';
                    if (j + 1 < ref.childIndices.size()) pageOss << "CHILD|" << pageIds[ref.childIndices[j + 1]] << '\n';
                }
            }
            pageOss << "ENDPAGE\n";
            std::string page = pageOss.str();
            ensure(page.size() <= kTidPageSize, "page content exceeds page size");
            ofs.write(page.data(), static_cast<std::streamsize>(page.size()));
            padToPageSize(ofs, static_cast<std::uint32_t>(page.size()));
        }
        index_.clearDirtyFlags();
        return;
    }
}

void Table::writeHeader(std::ostream& os) {
    std::ostringstream headerOss;
    headerOss << "TID_PAGED_V3\npage_size=" << kTidPageSize << "\nroot_page=" << rootPageId_
              << "\nnext_page=" << nextPageId_ << '\n';
    std::string header = headerOss.str();
    os.write(header.data(), static_cast<std::streamsize>(header.size()));
    padToPageSize(os, static_cast<std::uint32_t>(header.size()));
}

void Table::loadIndexFromTid() {
    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();
    if (tryLoadPagedTid()) {
        return;
    }
    rebuildIndexFromData();
}

void Table::rebuildIndexFromData() {
    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();

    dataPages_.scan([&](TupleRef ref, const Row& row) {
        if (row.values.empty()) return;
        const std::string key = row.values.front();
        index_.insert(key, Row{{key}});
        std::uint64_t packed = ref.pack();
        primaryKeyOffsets_[key] = packed;
        primaryKeyOffsetsOrdered_[key] = packed;
    });
    syncIndexPages();
}

void Table::initializeTidFile() {
    std::ofstream ofs(indexFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to initialize table index file: " + indexFilePath().string());
    rootPageId_ = 1;
    nextPageId_ = 2;
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();

    ofs << "TID_PAGED_V3\n";
    ofs << "page_size=4096\n";
    ofs << "root_page=" << rootPageId_ << '\n';
    ofs << "PAGE|1|leaf=1|parent=0|prev=0|next=0|entry_count=0\n";
    ofs << "ENDPAGE\n";
}

bool Table::tryLoadPagedTid() {
    std::ifstream ifs(indexFilePath());
    if (!ifs.good()) {
        return false;
    }

    std::string magic;
    std::getline(ifs, magic);

    // V3 format: fixed-size pages
    if (magic == "TID_PAGED_V3") {
        rootPageId_ = 1;
        nextPageId_ = 2;

        struct V3ParsedPage {
            std::uint32_t pageId = 0; std::uint32_t parentPageId = 0;
            std::uint32_t prevPageId = 0; std::uint32_t nextPageId = 0;
            bool isLeaf = true;
            std::vector<std::string> keys; std::vector<std::uint64_t> offsets;
            std::vector<std::uint32_t> childPageIds;
        };
        std::unordered_map<std::uint32_t, V3ParsedPage> pages;
        std::string hdrLine;
        std::uint32_t maxPageId = 0;
        while (std::getline(ifs, hdrLine)) {
            if (hdrLine.rfind("root_page=", 0) == 0) rootPageId_ = static_cast<std::uint32_t>(std::stoul(hdrLine.substr(10)));
            else if (hdrLine.rfind("next_page=", 0) == 0) nextPageId_ = static_cast<std::uint32_t>(std::stoul(hdrLine.substr(10)));
            else if (hdrLine.rfind("PAGE|", 0) == 0) break;
        }

        for (std::uint32_t pg = 1; ; ++pg) {
            std::streamoff pageOffset = static_cast<std::streamoff>(pg) * kTidPageSize;
            ifs.clear(); ifs.seekg(pageOffset, std::ios::beg);
            if (!ifs.good()) break;
            std::string pageContent(kTidPageSize, '\0');
            ifs.read(pageContent.data(), kTidPageSize);
            if (ifs.gcount() == 0) break;

            V3ParsedPage current; bool inPage = false;
            std::istringstream pageStream(pageContent);
            std::string pline;
            while (std::getline(pageStream, pline)) {
                if (pline.empty() || pline[0] == '\0') continue;
                if (pline.rfind("PAGE|", 0) == 0) {
                    inPage = true;
                    std::vector<std::string> parts = split(pline, '|');
                    if (parts.size() >= 2) current.pageId = static_cast<std::uint32_t>(std::stoul(parts[1]));
                    for (std::size_t pi = 2; pi < parts.size(); ++pi) {
                        const auto& p = parts[pi];
                        if (p.rfind("leaf=", 0) == 0) current.isLeaf = (p.size() >= 6 && p[5] == '1');
                        else if (p.rfind("parent=", 0) == 0) current.parentPageId = static_cast<std::uint32_t>(std::stoul(p.substr(7)));
                        else if (p.rfind("prev=", 0) == 0) current.prevPageId = static_cast<std::uint32_t>(std::stoul(p.substr(5)));
                        else if (p.rfind("next=", 0) == 0) current.nextPageId = static_cast<std::uint32_t>(std::stoul(p.substr(5)));
                    }
                    if (current.pageId > maxPageId) maxPageId = current.pageId;
                    if (current.pageId >= nextPageId_) nextPageId_ = current.pageId + 1;
                    continue;
                }
                if (!inPage) continue;
                if (pline.rfind("ENTRY|", 0) == 0) {
                    std::vector<std::string> parts = split(pline, '|');
                    if (parts.size() >= 2 && !parts[1].empty()) {
                        current.keys.push_back(parts[1]);
                        if (parts.size() >= 3) { try { current.offsets.push_back(static_cast<std::uint64_t>(std::stoull(parts[2]))); } catch (...) { current.offsets.push_back(0); } }
                        else { current.offsets.push_back(0); }
                    }
                    continue;
                }
                if (pline.rfind("CHILD|", 0) == 0) {
                    std::vector<std::string> parts = split(pline, '|');
                    if (parts.size() >= 2) { try { current.childPageIds.push_back(static_cast<std::uint32_t>(std::stoul(parts[1]))); } catch (...) {} }
                    continue;
                }
                if (pline == "ENDPAGE") {
                    if (inPage && current.pageId > 0) pages[current.pageId] = std::move(current);
                    current = V3ParsedPage{}; inPage = false;
                }
            }
            if (inPage && current.pageId > 0) pages[current.pageId] = std::move(current);
            if (pg > maxPageId + 10) break;
        }

        if (pages.empty()) return false;

        std::uint32_t leafId = rootPageId_;
        {
            auto it = pages.find(rootPageId_);
            if (it != pages.end() && !it->second.isLeaf) {
                std::uint32_t cursor = rootPageId_;
                while (true) {
                    auto cit = pages.find(cursor);
                    if (cit == pages.end()) break;
                    if (cit->second.isLeaf) { leafId = cursor; break; }
                    if (cit->second.childPageIds.empty()) break;
                    cursor = cit->second.childPageIds.front();
                }
            }
        }
        std::unordered_set<std::uint32_t> visited;
        while (leafId != 0 && visited.insert(leafId).second) {
            auto pit = pages.find(leafId);
            if (pit == pages.end() || !pit->second.isLeaf) break;
            const auto& pg = pit->second;
            for (std::size_t i = 0; i < pg.keys.size(); ++i) {
                std::uint64_t off = (i < pg.offsets.size()) ? pg.offsets[i] : 0;
                index_.insert(pg.keys[i], Row{{pg.keys[i]}});
                primaryKeyOffsets_[pg.keys[i]] = off;
                primaryKeyOffsetsOrdered_[pg.keys[i]] = off;
            }
            leafId = pg.nextPageId;
        }
        for (const auto& kv : pages) {
            const auto& pg = kv.second;
            if (pg.isLeaf && visited.count(pg.pageId)) continue;
            for (std::size_t i = 0; i < pg.keys.size(); ++i) {
                std::uint64_t off = (i < pg.offsets.size()) ? pg.offsets[i] : 0;
                index_.insert(pg.keys[i], Row{{pg.keys[i]}});
                primaryKeyOffsets_[pg.keys[i]] = off;
                primaryKeyOffsetsOrdered_[pg.keys[i]] = off;
            }
        }
        {
            std::vector<std::uint32_t> pageOrder;
            std::function<void(std::uint32_t)> collectOrder = [&](std::uint32_t pid) {
                pageOrder.push_back(pid);
                auto it = pages.find(pid);
                if (it != pages.end() && !it->second.isLeaf) {
                    for (auto childId : it->second.childPageIds) {
                        collectOrder(childId);
                    }
                }
            };
            collectOrder(rootPageId_);
            index_.assignPageIdsFrom(pageOrder);
        }
        {
            std::vector<std::uint32_t> pageOrder;
            std::function<void(std::uint32_t)> collectOrder = [&](std::uint32_t pid) {
                pageOrder.push_back(pid);
                auto it = pages.find(pid);
                if (it != pages.end() && !it->second.isLeaf) {
                    for (auto childId : it->second.childPageIds) {
                        collectOrder(childId);
                    }
                }
            };
            collectOrder(rootPageId_);
            index_.assignPageIdsFrom(pageOrder);
        }
        return true;
    }

    // V1 fallback
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
            try {
                primaryKeyOffsets_[parts[1]] = static_cast<std::uint64_t>(std::stoull(parts[2]));
                primaryKeyOffsetsOrdered_[parts[1]] = primaryKeyOffsets_[parts[1]];
            } catch (...) {
            }
        }
    }
    return true;
}

std::vector<std::string> Table::normalizeInputValues(const std::vector<std::string>& values) const {
    return ConstraintValidator(*this).normalize(values);
}

bool Table::validateConstraintForExistingRows(const ColumnConstraintSpec& spec) const {
    return ConstraintValidator(*this).checkNewConstraint(spec);
}

void Table::enforceRowConstraints(const std::vector<std::string>& values,
                                   const std::string* skipPrimaryKey) const {
    ConstraintValidator(*this).check(values, skipPrimaryKey);
}

std::vector<std::string> Table::ConstraintValidator::normalize(const std::vector<std::string>& values) const {
    const auto& schema = table.schema_;
    ensure(values.size() <= schema.columns.size(),
           "column count mismatch, expected <= " + std::to_string(schema.columns.size()) +
               ", got " + std::to_string(values.size()));
    std::vector<std::string> normalized(schema.columns.size(), "");
    for (std::size_t i = 0; i < values.size(); ++i) {
        normalized[i] = values[i];
    }
    for (std::size_t i = values.size(); i < schema.columns.size(); ++i) {
        if (i < schema.columnMetas.size() && !schema.columnMetas[i].defaultValue.empty()) {
            normalized[i] = schema.columnMetas[i].defaultValue;
        }
    }

    // AUTO_INCREMENT 值自动生成
    // 作者：NAPH130
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        if (normalized[i].empty() && i < schema.columnMetas.size()
            && (schema.columnMetas[i].integrities & 8) != 0) {
            normalized[i] = std::to_string(table.nextAutoIncValue(schema.columns[i]));
        }
    }

    return normalized;
}

std::int64_t Table::nextAutoIncValue(const std::string& columnName) const {
    auto it = autoIncCounters_.find(columnName);
    if (it == autoIncCounters_.end()) {
        autoIncCounters_[columnName] = 2;
        return 1;
    }
    return it->second++;
}

void Table::initAutoIncValue(const std::string& columnName, std::int64_t startValue) {
    autoIncCounters_[columnName] = startValue;
}

bool Table::isAutoIncColumn(const std::string& columnName) const {
    for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
        if (schema_.columns[i] == columnName
            && i < schema_.columnMetas.size()
            && (schema_.columnMetas[i].integrities & 8) != 0) {
            return true;
        }
    }
    return false;
}

void Table::ConstraintValidator::check(const std::vector<std::string>& values,
                                        const std::string* skipPrimaryKey) const {
    const auto& schema = table.schema_;
    const auto& constraints = table.constraintsByColumn_;
    for (std::size_t i = 0; i < schema.columns.size(); ++i) {
        const std::string& col = schema.columns[i];
        const auto it = constraints.find(col);
        if (it == constraints.end()) continue;
        const auto& spec = it->second;
        if (spec.notNull) {
            ensure(!values[i].empty(), "NOT NULL constraint violation on column: " + col);
        }
        if (spec.unique) {
            auto si = table.secondaryIndexes_.find(col);
            if (si != table.secondaryIndexes_.end() && si->second.active && i > 0) {
                std::vector<std::uint64_t> offsets;
                si->second.lookup(values[i], Table::CompareOp::EQ, "", {}, offsets);
                if (!offsets.empty()) {
                    if (skipPrimaryKey == nullptr) {
                        ensure(false, "UNIQUE constraint violation on column: " + col);
                    } else {
                        auto pkOff = table.primaryKeyOffsets_.find(*skipPrimaryKey);
                        bool hasOther = false;
                        for (auto off : offsets) {
                            if (pkOff == table.primaryKeyOffsets_.end() || off != pkOff->second) {
                                hasOther = true; break;
                            }
                        }
                        ensure(!hasOther, "UNIQUE constraint violation on column: " + col);
                    }
                }
            } else {
                const auto rows = table.readAllDataRows();
                for (const auto& row : rows) {
                    if (i >= row.values.size()) continue;
                    if (skipPrimaryKey != nullptr && !row.values.empty() && row.values.front() == *skipPrimaryKey) continue;
                    ensure(row.values[i] != values[i], "UNIQUE constraint violation on column: " + col);
                }
            }
        }
        if (spec.hasCheck) {
            const auto sep = spec.checkExpr.find('|');
            if (sep == std::string::npos) continue;
            std::string op = spec.checkExpr.substr(0, sep);
            std::string expected = spec.checkExpr.substr(sep + 1);
            double leftNum = 0, rightNum = 0;
            bool leftIsNum = false, rightIsNum = false;
            { errno = 0; char* e = nullptr; leftNum = std::strtod(values[i].c_str(), &e); leftIsNum = (e != values[i].c_str() && *e == '\0' && errno != ERANGE); }
            { errno = 0; char* e = nullptr; rightNum = std::strtod(expected.c_str(), &e); rightIsNum = (e != expected.c_str() && *e == '\0' && errno != ERANGE); }
            bool ok = false;
            if (leftIsNum && rightIsNum) {
                if (op == "<") ok = (leftNum < rightNum);
                else if (op == "<=") ok = (leftNum <= rightNum);
                else if (op == ">") ok = (leftNum > rightNum);
                else if (op == ">=") ok = (leftNum >= rightNum);
                else if (op == "=" || op == "==") ok = (leftNum == rightNum);
                else if (op == "!=" || op == "<>") ok = (leftNum != rightNum);
            } else {
                if (op == "=" || op == "==") ok = (values[i] == expected);
                else if (op == "!=" || op == "<>") ok = (values[i] != expected);
            }
            ensure(ok, "CHECK constraint violation on column: " + col + " (" + spec.checkExpr + ")");
        }
    }
}

bool Table::ConstraintValidator::checkNewConstraint(const ColumnConstraintSpec& spec) const {
    const std::size_t idx = table.columnIndex(spec.column);
    const auto rows = table.readAllDataRows();
    if (spec.notNull) {
        for (const auto& row : rows) {
            if (idx >= row.values.size() || row.values[idx].empty()) return false;
        }
    }
    if (spec.unique) {
        std::unordered_set<std::string> seen;
        for (const auto& row : rows) {
            if (idx >= row.values.size()) continue;
            if (!seen.insert(row.values[idx]).second) return false;
        }
    }
    if (spec.hasCheck) {
        const auto sep = spec.checkExpr.find('|');
        if (sep == std::string::npos) return false;
        std::string op = spec.checkExpr.substr(0, sep);
        std::string expected = spec.checkExpr.substr(sep + 1);
        for (const auto& row : rows) {
            if (idx >= row.values.size()) continue;
            double leftNum = 0, rightNum = 0;
            bool leftIsNum = false, rightIsNum = false;
            { errno = 0; char* e = nullptr; leftNum = std::strtod(row.values[idx].c_str(), &e); leftIsNum = (e != row.values[idx].c_str() && *e == '\0' && errno != ERANGE); }
            { errno = 0; char* e = nullptr; rightNum = std::strtod(expected.c_str(), &e); rightIsNum = (e != expected.c_str() && *e == '\0' && errno != ERANGE); }
            bool ok = false;
            if (leftIsNum && rightIsNum) {
                if (op == "<") ok = (leftNum < rightNum);
                else if (op == "<=") ok = (leftNum <= rightNum);
                else if (op == ">") ok = (leftNum > rightNum);
                else if (op == ">=") ok = (leftNum >= rightNum);
                else if (op == "=" || op == "==") ok = (leftNum == rightNum);
                else if (op == "!=" || op == "<>") ok = (leftNum != rightNum);
            } else {
                if (op == "=" || op == "==") ok = (row.values[idx] == expected);
                else if (op == "!=" || op == "<>") ok = (row.values[idx] != expected);
            }
            if (!ok) return false;
        }
    }
    return true;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>>
Table::buildUniqueCountersForQuery(const std::vector<QueryConstraint>& queryConstraints) const {
    std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>> counters;
    std::unordered_set<std::string> targets;
    for (const auto& qc : queryConstraints) {
        if (qc.type == ConstraintType::UNIQUE) {
            targets.insert(qc.column);
        }
    }
    if (targets.empty()) {
        return counters;
    }
    const auto rows = readAllDataRows();
    for (const auto& col : targets) {
        const std::size_t idx = columnIndex(col);
        auto& counter = counters[col];
        for (const auto& row : rows) {
            if (idx < row.values.size()) {
                ++counter[row.values[idx]];
            }
        }
    }
    return counters;
}

bool Table::matchQueryConstraints(
    const Row& row,
    const std::vector<QueryConstraint>& queryConstraints,
    const std::unordered_map<std::string, std::unordered_map<std::string, std::size_t>>& uniqueCounters) const {
    for (const auto& qc : queryConstraints) {
        const std::size_t idx = columnIndex(qc.column);
        if (idx >= row.values.size()) {
            return false;
        }
        const auto specIt = constraintsByColumn_.find(qc.column);
        const bool hasSpec = specIt != constraintsByColumn_.end();
        if (qc.type == ConstraintType::NOT_NULL) {
            const bool ok = hasSpec && specIt->second.notNull && !row.values[idx].empty();
            if ((qc.satisfy && !ok) || (!qc.satisfy && ok)) {
                return false;
            }
        } else if (qc.type == ConstraintType::DEFAULT_VALUE) {
            const bool ok = hasSpec && specIt->second.hasDefault && row.values[idx] == specIt->second.defaultValue;
            if ((qc.satisfy && !ok) || (!qc.satisfy && ok)) {
                return false;
            }
        } else if (qc.type == ConstraintType::UNIQUE) {
            auto tableIt = uniqueCounters.find(qc.column);
            std::size_t freq = 0;
            if (tableIt != uniqueCounters.end()) {
                auto vIt = tableIt->second.find(row.values[idx]);
                if (vIt != tableIt->second.end()) {
                    freq = vIt->second;
                }
            }
            const bool ok = hasSpec && specIt->second.unique && freq == 1;
            if ((qc.satisfy && !ok) || (!qc.satisfy && ok)) {
                return false;
            }
        }
    }
    return true;
}

std::size_t Table::columnIndex(const std::string& columnName) const {
    const auto it = std::find(schema_.columns.begin(), schema_.columns.end(), columnName);
    ensure(it != schema_.columns.end(), "unknown column: " + columnName);
    return static_cast<std::size_t>(std::distance(schema_.columns.begin(), it));
}

bool Table::matchWhere(const Row& row, const std::vector<WhereCondition>& whereConditions) const {
    for (const auto& condition : whereConditions) {
        const std::size_t idx = columnIndex(condition.column);
        if (idx >= row.values.size()) return false;
        if (condition.op == CompareOp::IN || condition.op == CompareOp::BETWEEN || condition.op == CompareOp::LIKE) {
            if (!compareValue(row.values[idx], condition)) return false;
        } else {
            if (!compareTyped(idx, row.values[idx], condition.op, condition.value)) return false;
        }
    }
    return true;
}

bool Table::matchConditionTree(const Row& row, const std::shared_ptr<ConditionNode>& node) const {
    if (!node) return true;
    if (node->isLeaf) {
        const auto& cond = node->condition;
        if (cond.isExistsCheck) {
            bool hasRows = !cond.values.empty() && cond.values[0] == "1";
            return cond.isSubqueryNot ? !hasRows : hasRows;
        }
        const std::size_t idx = columnIndex(cond.column);
        if (idx >= row.values.size()) return false;
        bool match;
        if (cond.op == CompareOp::IN || cond.op == CompareOp::BETWEEN || cond.op == CompareOp::LIKE)
            match = compareValue(row.values[idx], cond);
        else
            match = compareTyped(idx, row.values[idx], cond.op, cond.value);
        return cond.isSubqueryNot ? !match : match;
    }
    const bool leftMatch = matchConditionTree(row, node->left);
    const bool rightMatch = matchConditionTree(row, node->right);
    return node->logicalOp == LogicalOp::OR ? (leftMatch || rightMatch) : (leftMatch && rightMatch);
}

bool Table::hasIndexForColumn(const std::string& column) const {
    if (!schema_.columns.empty() && column == schema_.columns.front()) return true;
    auto it = secondaryIndexes_.find(column);
    return it != secondaryIndexes_.end() && it->second.active;
}

bool Table::canUseIndexForCondition(const WhereCondition& condition) const {
    if (!hasIndexForColumn(condition.column)) {
        return false;
    }
    return condition.op != CompareOp::LIKE;
}

bool Table::lookupOffsetsByIndexedRequest(const IndexedLookupRequest& request,
                                          std::vector<std::uint64_t>& offsets) const {
    if (lookupOffsetsByPrimaryIndex(request, offsets)) {
        return true;
    }
    return lookupOffsetsBySecondaryIndex(request, offsets);
}

bool Table::lookupOffsetsByPrimaryIndex(const IndexedLookupRequest& request,
                                         std::vector<std::uint64_t>& offsets) const {
    if (schema_.columns.empty() || request.column != schema_.columns.front()) {
        return false;
    }
    offsets.clear();
    switch (request.op) {
        case CompareOp::EQ: {
            const auto it = primaryKeyOffsets_.find(request.value);
            if (it != primaryKeyOffsets_.end()) {
                offsets.push_back(it->second);
            }
            break;
        }
        case CompareOp::IN: {
            for (const auto& key : request.values) {
                const auto it = primaryKeyOffsets_.find(key);
                if (it != primaryKeyOffsets_.end()) {
                    offsets.push_back(it->second);
                }
            }
            break;
        }
        case CompareOp::GT: {
            for (auto it = primaryKeyOffsetsOrdered_.upper_bound(request.value);
                 it != primaryKeyOffsetsOrdered_.end();
                 ++it) {
                offsets.push_back(it->second);
            }
            break;
        }
        case CompareOp::GE: {
            for (auto it = primaryKeyOffsetsOrdered_.lower_bound(request.value);
                 it != primaryKeyOffsetsOrdered_.end();
                 ++it) {
                offsets.push_back(it->second);
            }
            break;
        }
        case CompareOp::LT: {
            for (auto it = primaryKeyOffsetsOrdered_.begin();
                 it != primaryKeyOffsetsOrdered_.lower_bound(request.value);
                 ++it) {
                offsets.push_back(it->second);
            }
            break;
        }
        case CompareOp::LE: {
            for (auto it = primaryKeyOffsetsOrdered_.begin();
                 it != primaryKeyOffsetsOrdered_.upper_bound(request.value);
                 ++it) {
                offsets.push_back(it->second);
            }
            break;
        }
        case CompareOp::BETWEEN: {
            auto beginIt = primaryKeyOffsetsOrdered_.lower_bound(request.value);
            auto endIt = primaryKeyOffsetsOrdered_.upper_bound(request.secondValue);
            for (auto it = beginIt; it != endIt; ++it) {
                offsets.push_back(it->second);
            }
            break;
        }
        case CompareOp::NE: {
            for (const auto& entry : primaryKeyOffsetsOrdered_) {
                if (entry.first != request.value) {
                    offsets.push_back(entry.second);
                }
            }
            break;
        }
        default:
            return false;
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return true;
}

bool Table::lookupOffsetsBySecondaryIndex(const IndexedLookupRequest& request,
                                           std::vector<std::uint64_t>& offsets) const {
    offsets.clear();
    auto it = secondaryIndexes_.find(request.column);
    if (it == secondaryIndexes_.end() || !it->second.active) return false;
    return it->second.lookup(request.value, request.op, request.secondValue, request.values, offsets);
}

bool Table::lookupOffsetsByIndexedCondition(const WhereCondition& condition,
                                            std::vector<std::uint64_t>& offsets) const {
    if (!canUseIndexForCondition(condition)) {
        return false;
    }
    IndexedLookupRequest request;
    request.column = condition.column;
    request.op = condition.op;
    request.value = condition.value;
    request.secondValue = condition.secondValue;
    request.values = condition.values;
    return lookupOffsetsByIndexedRequest(request, offsets);
}

Table::IndexCandidateResult Table::collectIndexCandidates(const std::shared_ptr<ConditionNode>& node) const {
    if (!node) {
        return {};
    }
    if (node->isLeaf) {
        IndexCandidateResult result;
        result.constrained = lookupOffsetsByIndexedCondition(node->condition, result.offsets);
        return result;
    }

    const IndexCandidateResult left = collectIndexCandidates(node->left);
    const IndexCandidateResult right = collectIndexCandidates(node->right);
    IndexCandidateResult merged;
    if (node->logicalOp == LogicalOp::AND) {
        if (left.constrained && right.constrained) {
            merged.constrained = true;
            merged.offsets = mergeOffsetIntersection(left.offsets, right.offsets);
            return merged;
        }
        return left.constrained ? left : right;
    }

    if (left.constrained && right.constrained) {
        merged.constrained = true;
        merged.offsets = mergeOffsetUnion(left.offsets, right.offsets);
    }
    return merged;
}

std::vector<std::uint64_t> Table::mergeOffsetUnion(const std::vector<std::uint64_t>& left,
                                                   const std::vector<std::uint64_t>& right) {
    std::vector<std::uint64_t> merged;
    merged.reserve(left.size() + right.size());
    merged.insert(merged.end(), left.begin(), left.end());
    merged.insert(merged.end(), right.begin(), right.end());
    std::sort(merged.begin(), merged.end());
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    return merged;
}

std::vector<std::uint64_t> Table::mergeOffsetIntersection(const std::vector<std::uint64_t>& left,
                                                          const std::vector<std::uint64_t>& right) {
    std::vector<std::uint64_t> result;
    if (left.empty() || right.empty()) {
        return result;
    }
    std::vector<std::uint64_t> leftSorted = left;
    std::vector<std::uint64_t> rightSorted = right;
    std::sort(leftSorted.begin(), leftSorted.end());
    std::sort(rightSorted.begin(), rightSorted.end());
    std::set_intersection(leftSorted.begin(),
                          leftSorted.end(),
                          rightSorted.begin(),
                          rightSorted.end(),
                          std::back_inserter(result));
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

bool Table::compareValue(const std::string& left, CompareOp op, const std::string& right) {
    if (op == CompareOp::LIKE) return likeMatch(left, right);
    double lv=0, rv=0;
    bool li=false, ri=false;
    { errno=0; char* e=nullptr; lv=std::strtod(left.c_str(),&e); li=(e!=left.c_str()&&*e=='\0'&&errno!=ERANGE); }
    { errno=0; char* e=nullptr; rv=std::strtod(right.c_str(),&e); ri=(e!=right.c_str()&&*e=='\0'&&errno!=ERANGE); }
    if (li && ri) {
        switch (op) { case CompareOp::EQ: return lv==rv; case CompareOp::NE: return lv!=rv; case CompareOp::GT: return lv>rv; case CompareOp::GE: return lv>=rv; case CompareOp::LT: return lv<rv; case CompareOp::LE: return lv<=rv; }
    }
    switch (op) { case CompareOp::EQ: return left==right; case CompareOp::NE: return left!=right; case CompareOp::GT: return left>right; case CompareOp::GE: return left>=right; case CompareOp::LT: return left<right; case CompareOp::LE: return left<=right; }
    return false;
}

bool Table::compareTyped(std::size_t colIndex, const std::string& left, CompareOp op, const std::string& right) const {
    if (op == CompareOp::LIKE) return likeMatch(left, right);
    if (op == CompareOp::IN || op == CompareOp::BETWEEN) return false;

    if (colIndex < schema_.columnMetas.size()) {
        DataType dt = schema_.columnMetas[colIndex].dataType;
        if (dt == DataType::INT || dt == DataType::FLOAT) {
            double lv = 0, rv = 0;
            bool li = false, ri = false;
            { errno=0; char* e=nullptr; lv=std::strtod(left.c_str(),&e); li=(e!=left.c_str()&&*e=='\0'&&errno!=ERANGE); }
            { errno=0; char* e=nullptr; rv=std::strtod(right.c_str(),&e); ri=(e!=right.c_str()&&*e=='\0'&&errno!=ERANGE); }
            if (li && ri) {
                switch (op) {
                    case CompareOp::EQ: return lv == rv;
                    case CompareOp::NE: return lv != rv;
                    case CompareOp::GT: return lv > rv;
                    case CompareOp::GE: return lv >= rv;
                    case CompareOp::LT: return lv < rv;
                    case CompareOp::LE: return lv <= rv;
                }
            }
        }
    }
    switch (op) {
        case CompareOp::EQ: return left == right;
        case CompareOp::NE: return left != right;
        case CompareOp::GT: return left > right;
        case CompareOp::GE: return left >= right;
        case CompareOp::LT: return left < right;
        case CompareOp::LE: return left <= right;
    }
    return false;
}

bool Table::compareValue(const std::string& left, const WhereCondition& condition) {
    if (condition.op == CompareOp::IN) {
        return std::find(condition.values.begin(), condition.values.end(), left) != condition.values.end();
    }
    if (condition.op == CompareOp::BETWEEN) {
        if (condition.secondValue.empty()) {
            return false;
        }
        return compareValue(left, CompareOp::GE, condition.value)
               && compareValue(left, CompareOp::LE, condition.secondValue);
    }
    return compareValue(left, condition.op, condition.value);
}

std::string Table::makePrimaryKey(const std::vector<std::string>& values) const {
    return values.empty() ? std::string() : values.front();
}

} // namespace storage
