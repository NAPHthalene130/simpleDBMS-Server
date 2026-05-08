#include "Table.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <sstream>
#include <unordered_set>

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

} // namespace

Table::Table(std::filesystem::path dbPath, TableSchema schema)
    : dbPath_(std::move(dbPath)), schema_(std::move(schema)), index_(2) {}

Table Table::create(const std::filesystem::path& dbPath,
                    const std::string& tableName,
                    const std::vector<std::string>& columns) {
    std::vector<ColumnDefinition> defs;
    defs.reserve(columns.size());
    for (const auto& c : columns) {
        defs.push_back(ColumnDefinition{c, ColumnConstraintSpec{c}});
    }
    return create(dbPath, tableName, defs);
}

Table Table::create(const std::filesystem::path& dbPath,
                    const std::string& tableName,
                    const std::vector<ColumnDefinition>& columns) {
    ensure(!tableName.empty(), "table name cannot be empty");
    ensure(!columns.empty(), "table must contain at least one column");
    std::vector<std::string> columnNames;
    columnNames.reserve(columns.size());
    for (const auto& col : columns) {
        ensure(!col.name.empty(), "column name cannot be empty");
        columnNames.push_back(col.name);
    }

    Table table(dbPath, TableSchema{tableName, columnNames});
    for (const auto& col : columns) {
        ColumnConstraintSpec spec = col.constraints;
        spec.column = col.name;
        table.constraintsByColumn_[col.name] = spec;
    }
    if (!columnNames.empty()) {
        table.constraintsByColumn_[columnNames.front()].notNull = true;
        table.constraintsByColumn_[columnNames.front()].unique = true;
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
        std::ofstream secondaryOfs(table.nonPrimaryIndexFilePath(columns[i].name), std::ios::app);
        ensure(secondaryOfs.good(),
               "failed to create non-primary index reserve file: "
               + table.nonPrimaryIndexFilePath(columns[i].name).string());
    }

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
    for (const auto& col : parsedColumns) {
        table.constraintsByColumn_[col].column = col;
    }
    if (!parsedColumns.empty()) {
        table.constraintsByColumn_[parsedColumns.front()].notNull = true;
        table.constraintsByColumn_[parsedColumns.front()].unique = true;
    }

    table.loadIndexFromTid();
    table.loadConstraintsFromIntegrityMeta();
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
    const std::uint64_t offset = appendDataRow(normalized);
    appendIndexEntry(primaryKey, offset);
}

bool Table::updateByPrimaryKey(const std::string& primaryKey,
                               const std::vector<std::string>& newValues) {
    if (primaryKey.empty() || newValues.empty()) {
        return false;
    }
    const std::vector<std::string> normalized = normalizeInputValues(newValues);

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

    const std::string newPrimaryKey = makePrimaryKey(normalized);
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

    enforceRowConstraints(normalized, &primaryKey);
    rows[hitIndex] = Row{normalized};
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
        std::ifstream ifs(dataFilePath());
        ensure(ifs.good(), "failed to open table data file: " + dataFilePath().string());
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.rfind("ROW|", 0) != 0) {
                continue;
            }

            Row row = deserializeRow(line.substr(4));
            if (row.values.size() != schema_.columns.size()) {
                continue;
            }
            if (!matchConditionTree(row, whereTree)) {
                continue;
            }
            matchedRows.push_back(std::move(row));
        }
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
    std::ifstream ifs(dataFilePath());
    ensure(ifs.good(), "failed to open table data file: " + dataFilePath().string());
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("ROW|", 0) != 0) {
            continue;
        }
        Row row = deserializeRow(line.substr(4));
        if (row.values.size() != schema_.columns.size()) {
            continue;
        }
        if (!matchConditionTree(row, whereTree)) {
            continue;
        }
        rows.push_back(std::move(row));
    }

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
    ensure(validateConstraintForExistingRows(merged), "constraint conflicts with existing rows");
    constraintsByColumn_[spec.column] = merged;
    flushIntegrityMeta();
    return true;
}

bool Table::addColumnConstraints(const std::vector<ColumnConstraintSpec>& specs) {
    auto backup = constraintsByColumn_;
    try {
        for (const auto& spec : specs) {
            addColumnConstraint(spec);
        }
        return true;
    } catch (...) {
        constraintsByColumn_ = std::move(backup);
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
    for (std::size_t i = 1; i < schema_.columns.size(); ++i) {
        ofs << "index_reserved=" << schema_.columns[i] << ":BTREE:" << schema_.name << "."
            << schema_.columns[i] << ".nidx\n";
    }
}

void Table::flushIntegrityMeta() const {
    std::ofstream ofs(integrityFilePath(), std::ios::trunc);
    ensure(ofs.good(), "failed to write table integrity file: " + integrityFilePath().string());

    ofs << "constraints_version=1\n";
    ofs << "constraint=PRIMARY_KEY(" << schema_.columns.front() << ")\n";
    ofs << "index=PRIMARY:" << schema_.name << ".tid\n";
    for (std::size_t i = 1; i < schema_.columns.size(); ++i) {
        ofs << "index_reserved=" << schema_.columns[i] << ":" << schema_.name << "."
            << schema_.columns[i] << ".nidx\n";
    }
    for (const auto& col : schema_.columns) {
        const auto it = constraintsByColumn_.find(col);
        if (it == constraintsByColumn_.end()) {
            continue;
        }
        const auto& spec = it->second;
        if (spec.notNull) {
            ofs << "constraint=NOT_NULL(" << col << ")\n";
        }
        if (spec.unique) {
            ofs << "constraint=UNIQUE(" << col << ")\n";
        }
        if (spec.hasDefault) {
            ofs << "constraint=DEFAULT(" << col << "|" << encodeConstraintValue(spec.defaultValue) << ")\n";
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
        if (line.rfind("constraint=NOT_NULL(", 0) == 0 && !line.empty() && line.back() == ')') {
            const std::string col = line.substr(20, line.size() - 21);
            constraintsByColumn_[col].column = col;
            constraintsByColumn_[col].notNull = true;
            continue;
        }
        if (line.rfind("constraint=UNIQUE(", 0) == 0 && !line.empty() && line.back() == ')') {
            const std::string col = line.substr(18, line.size() - 19);
            constraintsByColumn_[col].column = col;
            constraintsByColumn_[col].unique = true;
            continue;
        }
        if (line.rfind("constraint=DEFAULT(", 0) == 0 && !line.empty() && line.back() == ')') {
            const std::string body = line.substr(19, line.size() - 20);
            const auto sep = body.find('|');
            if (sep == std::string::npos || sep == 0) {
                continue;
            }
            const std::string col = body.substr(0, sep);
            constraintsByColumn_[col].column = col;
            constraintsByColumn_[col].hasDefault = true;
            constraintsByColumn_[col].defaultValue = decodeConstraintValue(body.substr(sep + 1));
        }
    }
    if (!schema_.columns.empty()) {
        constraintsByColumn_[schema_.columns.front()].column = schema_.columns.front();
        constraintsByColumn_[schema_.columns.front()].notNull = true;
        constraintsByColumn_[schema_.columns.front()].unique = true;
    }
}

std::uint64_t Table::appendDataRow(const std::vector<std::string>& values) const {
    const std::uint64_t offset = std::filesystem::exists(dataFilePath())
                                     ? static_cast<std::uint64_t>(std::filesystem::file_size(dataFilePath()))
                                     : 0;
    std::ofstream ofs(dataFilePath(), std::ios::app | std::ios::binary);
    ensure(ofs.good(), "failed to open table data file: " + dataFilePath().string());
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
    primaryKeyOffsets_[key] = offset;
    primaryKeyOffsetsOrdered_[key] = offset;
}

void Table::loadIndexFromTid() {
    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();
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
            try {
                primaryKeyOffsets_[key] = static_cast<std::uint64_t>(std::stoull(line.substr(sepPos + 1)));
                primaryKeyOffsetsOrdered_[key] = primaryKeyOffsets_[key];
            } catch (...) {
            }
        }
    }
    rebuildIndexFromData();
}

void Table::rebuildIndexFromData() {
    index_.clear();
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();

    std::ifstream ifs(dataFilePath(), std::ios::binary);
    if (!ifs.good()) {
        return;
    }
    initializeTidFile();
    std::ofstream tidOfs(indexFilePath(), std::ios::app);
    ensure(tidOfs.good(), "failed to rebuild table index file: " + indexFilePath().string());

    std::string line;
    while (true) {
        const std::streampos linePos = ifs.tellg();
        if (!std::getline(ifs, line)) {
            break;
        }
        if (linePos == std::streampos(-1)) {
            continue;
        }
        const std::uint64_t lineStartOffset = static_cast<std::uint64_t>(linePos);

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
        primaryKeyOffsets_[key] = lineStartOffset;
        primaryKeyOffsetsOrdered_[key] = lineStartOffset;
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
    primaryKeyOffsets_.clear();
    primaryKeyOffsetsOrdered_.clear();

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
            try {
                primaryKeyOffsets_[parts[1]] = static_cast<std::uint64_t>(std::stoull(parts[2]));
                primaryKeyOffsetsOrdered_[parts[1]] = primaryKeyOffsets_[parts[1]];
            } catch (...) {
            }
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

bool Table::readRowByOffset(std::uint64_t offset, Row& row) const {
    std::ifstream ifs(dataFilePath(), std::ios::binary);
    if (!ifs.good()) {
        return false;
    }
    ifs.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!ifs.good()) {
        return false;
    }
    std::string line;
    if (!std::getline(ifs, line)) {
        return false;
    }
    if (line.rfind("ROW|", 0) != 0) {
        return false;
    }
    row = deserializeRow(line.substr(4));
    return true;
}

void Table::rewriteDataRows(const std::vector<Row>& rows) const {
    std::ofstream ofs(dataFilePath(), std::ios::trunc | std::ios::binary);
    ensure(ofs.good(), "failed to rewrite table data file: " + dataFilePath().string());
    for (const auto& row : rows) {
        ofs << "ROW|" << serializeRow(row) << '\n';
    }
}

std::vector<std::string> Table::normalizeInputValues(const std::vector<std::string>& values) const {
    ensure(values.size() <= schema_.columns.size(),
           "column count mismatch, expected <= " + std::to_string(schema_.columns.size()) +
               ", got " + std::to_string(values.size()));
    std::vector<std::string> normalized(schema_.columns.size(), "");
    for (std::size_t i = 0; i < values.size(); ++i) {
        normalized[i] = values[i];
    }
    for (std::size_t i = values.size(); i < schema_.columns.size(); ++i) {
        const auto it = constraintsByColumn_.find(schema_.columns[i]);
        if (it != constraintsByColumn_.end() && it->second.hasDefault) {
            normalized[i] = it->second.defaultValue;
        }
    }
    return normalized;
}

bool Table::validateConstraintForExistingRows(const ColumnConstraintSpec& spec) const {
    const std::size_t idx = columnIndex(spec.column);
    const auto rows = readAllDataRows();
    if (spec.notNull) {
        for (const auto& row : rows) {
            if (idx >= row.values.size() || row.values[idx].empty()) {
                return false;
            }
        }
    }
    if (spec.unique) {
        std::unordered_set<std::string> seen;
        for (const auto& row : rows) {
            if (idx >= row.values.size()) {
                continue;
            }
            if (!seen.insert(row.values[idx]).second) {
                return false;
            }
        }
    }
    return true;
}

void Table::enforceRowConstraints(const std::vector<std::string>& values,
                                  const std::string* skipPrimaryKey) const {
    for (std::size_t i = 0; i < schema_.columns.size(); ++i) {
        const std::string& col = schema_.columns[i];
        const auto it = constraintsByColumn_.find(col);
        if (it == constraintsByColumn_.end()) {
            continue;
        }
        const auto& spec = it->second;
        const std::string& value = values[i];
        if (spec.notNull) {
            ensure(!value.empty(), "NOT NULL constraint violation on column: " + col);
        }
        if (spec.unique) {
            const auto rows = readAllDataRows();
            for (const auto& row : rows) {
                if (i >= row.values.size()) {
                    continue;
                }
                if (skipPrimaryKey != nullptr && !row.values.empty() && row.values.front() == *skipPrimaryKey) {
                    continue;
                }
                ensure(row.values[i] != value, "UNIQUE constraint violation on column: " + col);
            }
        }
    }
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
        if (idx >= row.values.size()) {
            return false;
        }
        if (!compareValue(row.values[idx], condition)) {
            return false;
        }
    }
    return true;
}

bool Table::matchConditionTree(const Row& row, const std::shared_ptr<ConditionNode>& node) const {
    if (!node) {
        return true;
    }
    if (node->isLeaf) {
        const std::size_t idx = columnIndex(node->condition.column);
        if (idx >= row.values.size()) {
            return false;
        }
        return compareValue(row.values[idx], node->condition);
    }
    const bool leftMatch = matchConditionTree(row, node->left);
    const bool rightMatch = matchConditionTree(row, node->right);
    return node->logicalOp == LogicalOp::OR ? (leftMatch || rightMatch) : (leftMatch && rightMatch);
}

bool Table::hasIndexForColumn(const std::string& column) const {
    return !schema_.columns.empty() && column == schema_.columns.front();
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
    if (!hasIndexForColumn(request.column)) {
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
    (void)request;
    offsets.clear();
    // Reserved extension point: future non-primary index providers can be plugged in here.
    return false;
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
    if (op == CompareOp::IN || op == CompareOp::BETWEEN) {
        return false;
    }
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
