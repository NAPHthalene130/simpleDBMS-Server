#include "DatabaseManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "log/LogWriter.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace {

constexpr const char *kTableBlockSeparator = "---TABLE_BLOCK---";

template <std::size_t N>
std::string arrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

template <std::size_t N>
std::array<char, N> stringToArray(const std::string &value)
{
    std::array<char, N> result{};
    const auto copyLen = std::min<std::size_t>(value.size(), N - 1);
    std::memcpy(result.data(), value.data(), copyLen);
    return result;
}

DateTime buildCurrentDateTime()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
    localtime_s(&localTime, &currentTime);
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch())
                              % 1000;

    DateTime dateTime;
    dateTime.setYear(static_cast<std::uint16_t>(localTime.tm_year + 1900));
    dateTime.setMonth(static_cast<std::uint16_t>(localTime.tm_mon + 1));
    dateTime.setDayOfWeek(static_cast<std::uint16_t>(localTime.tm_wday));
    dateTime.setDay(static_cast<std::uint16_t>(localTime.tm_mday));
    dateTime.setHour(static_cast<std::uint16_t>(localTime.tm_hour));
    dateTime.setMinute(static_cast<std::uint16_t>(localTime.tm_min));
    dateTime.setSecond(static_cast<std::uint16_t>(localTime.tm_sec));
    dateTime.setMilliseconds(static_cast<std::uint16_t>(milliseconds.count()));
    return dateTime;
}

std::string tableNameOf(const TableBlock &block)
{
    return arrayToString(block.getName());
}

std::string inferDbNameFromTableBlock(const TableBlock &tbInfo)
{
    const std::vector<std::array<char, 256>> pathValues = {
        tbInfo.getTdf(), tbInfo.getTrd(), tbInfo.getTic(), tbInfo.getTid()};

    for (const auto &pathValue : pathValues) {
        const std::filesystem::path filePath(arrayToString(pathValue));
        if (filePath.empty()) {
            continue;
        }
        const auto parent = filePath.parent_path();
        if (!parent.empty() && parent.has_filename()) {
            return parent.filename().string();
        }
    }
    return "";
}

std::vector<std::string> buildColumns(const TableBlock &tbInfo)
{
    std::vector<std::string> columns;
    const std::int32_t fieldNum = tbInfo.getFieldNum();
    if (fieldNum <= 0) {
        return {"id"};
    }

    columns.reserve(static_cast<std::size_t>(fieldNum));
    for (std::int32_t i = 1; i <= fieldNum; ++i) {
        columns.push_back("col" + std::to_string(i));
    }
    return columns;
}

TableBlock buildTableBlock(const std::filesystem::path &dbPath, const std::string &tableName)
{
    TableBlock block;
    block.setName(stringToArray<128>(tableName));
    block.setRecordNum(0);
    block.setFieldNum(0);
    block.setTdf(stringToArray<256>((dbPath / (tableName + ".tdf")).string()));
    block.setTrd(stringToArray<256>((dbPath / (tableName + ".trd")).string()));
    block.setTic(stringToArray<256>((dbPath / (tableName + ".tic")).string()));
    block.setTid(stringToArray<256>((dbPath / (tableName + ".tid")).string()));
    const DateTime now = buildCurrentDateTime();
    block.setCreateTime(now);
    block.setModifyTime(now);
    return block;
}

std::filesystem::path databaseDescriptorPath(const std::filesystem::path &dbPath)
{
    return dbPath / (dbPath.filename().string() + ".tb");
}

bool parseTableBlockLines(const std::filesystem::path &dbPath,
                          const std::vector<std::string> &lines,
                          TableBlock &out)
{
    if (lines.empty()) {
        return false;
    }

    if (lines.size() == 1) {
        const std::string line = lines.front();
        if (line.rfind("table=", 0) == 0 || line.find('=') == std::string::npos) {
            const std::string tableName = (line.rfind("table=", 0) == 0) ? line.substr(6) : line;
            if (tableName.empty()) {
                return false;
            }
            out = buildTableBlock(dbPath, tableName);
            return true;
        }
    }

    TableBlock parsed;
    if (!TableBlock::fromDescriptorLines(lines, parsed)) {
        return false;
    }
    const std::string name = tableNameOf(parsed);
    if (name.empty()) {
        return false;
    }
    out = parsed;
    if (arrayToString(out.getTdf()).empty()) out.setTdf(stringToArray<256>((dbPath / (name + ".tdf")).string()));
    if (arrayToString(out.getTic()).empty()) out.setTic(stringToArray<256>((dbPath / (name + ".tic")).string()));
    if (arrayToString(out.getTrd()).empty()) out.setTrd(stringToArray<256>((dbPath / (name + ".trd")).string()));
    if (arrayToString(out.getTid()).empty()) out.setTid(stringToArray<256>((dbPath / (name + ".tid")).string()));
    if (out.getCreateTime().getYear() == 0) out.setCreateTime(buildCurrentDateTime());
    if (out.getModifyTime().getYear() == 0) out.setModifyTime(out.getCreateTime());
    return true;
}

std::vector<TableBlock> readTableBlocksFromDescriptor(const std::filesystem::path &dbPath)
{
    std::vector<TableBlock> blocks;
    const auto descPath = databaseDescriptorPath(dbPath);
    if (!std::filesystem::exists(descPath)) {
        return blocks;
    }

    std::ifstream ifs(descPath);
    if (!ifs.good()) {
        return blocks;
    }

    std::vector<std::string> currentBlockLines;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line == kTableBlockSeparator) {
            TableBlock block;
            if (parseTableBlockLines(dbPath, currentBlockLines, block)) {
                blocks.push_back(block);
            }
            currentBlockLines.clear();
            continue;
        }
        if (!line.empty()) {
            currentBlockLines.push_back(line);
        }
    }
    TableBlock block;
    if (parseTableBlockLines(dbPath, currentBlockLines, block)) {
        blocks.push_back(block);
    }
    return blocks;
}

bool writeTableBlocksToDescriptor(const std::filesystem::path &dbPath, const std::vector<TableBlock> &blocks)
{
    const auto descPath = databaseDescriptorPath(dbPath);
    std::ofstream ofs(descPath, std::ios::trunc);
    if (!ofs.good()) {
        return false;
    }
    for (const auto &block : blocks) {
        const std::string tableName = tableNameOf(block);
        if (tableName.empty()) {
            continue;
        }
        for (const auto &line : block.toDescriptorLines()) {
            ofs << line << '\n';
        }
        ofs << kTableBlockSeparator << '\n';
    }
    return true;
}

TableBlock normalizeTableBlock(const std::filesystem::path &dbPath,
                               const std::string &tableName,
                               const TableBlock &source)
{
    TableBlock normalized = source;
    normalized.setName(stringToArray<128>(tableName));
    if (normalized.getFieldNum() < 0) {
        normalized.setFieldNum(0);
    }
    if (normalized.getRecordNum() < 0) {
        normalized.setRecordNum(0);
    }
    if (arrayToString(normalized.getTdf()).empty()) {
        normalized.setTdf(stringToArray<256>((dbPath / (tableName + ".tdf")).string()));
    }
    if (arrayToString(normalized.getTic()).empty()) {
        normalized.setTic(stringToArray<256>((dbPath / (tableName + ".tic")).string()));
    }
    if (arrayToString(normalized.getTrd()).empty()) {
        normalized.setTrd(stringToArray<256>((dbPath / (tableName + ".trd")).string()));
    }
    if (arrayToString(normalized.getTid()).empty()) {
        normalized.setTid(stringToArray<256>((dbPath / (tableName + ".tid")).string()));
    }
    if (normalized.getCreateTime().getYear() == 0) {
        normalized.setCreateTime(buildCurrentDateTime());
    }
    if (normalized.getModifyTime().getYear() == 0) {
        normalized.setModifyTime(normalized.getCreateTime());
    }
    return normalized;
}

bool upsertTableBlock(const std::filesystem::path &dbPath,
                      const std::string &tableName,
                      const TableBlock &block)
{
    auto blocks = readTableBlocksFromDescriptor(dbPath);
    const TableBlock normalized = normalizeTableBlock(dbPath, tableName, block);
    auto it = std::find_if(blocks.begin(),
                           blocks.end(),
                           [&tableName](const TableBlock &item) {
                               return tableNameOf(item) == tableName;
                           });
    if (it == blocks.end()) {
        blocks.push_back(normalized);
    } else {
        *it = normalized;
    }
    return writeTableBlocksToDescriptor(dbPath, blocks);
}

bool removeTableBlock(const std::filesystem::path &dbPath, const std::string &tableName)
{
    auto blocks = readTableBlocksFromDescriptor(dbPath);
    const auto newEnd = std::remove_if(blocks.begin(),
                                       blocks.end(),
                                       [&tableName](const TableBlock &block) {
                                           return tableNameOf(block) == tableName;
                                       });
    if (newEnd == blocks.end()) {
        return true;
    }
    blocks.erase(newEnd, blocks.end());
    return writeTableBlocksToDescriptor(dbPath, blocks);
}

bool readTableBlock(const std::filesystem::path &dbPath, const std::string &tableName, TableBlock &out)
{
    const auto blocks = readTableBlocksFromDescriptor(dbPath);
    const auto it = std::find_if(blocks.begin(),
                                 blocks.end(),
                                 [&tableName](const TableBlock &block) {
                                     return tableNameOf(block) == tableName;
                                 });
    if (it == blocks.end()) {
        return false;
    }
    out = *it;
    return true;
}

} // namespace

DatabaseManager::DatabaseManager(Core *core)
    : core(core)
{
}

bool DatabaseManager::createTable(TableBlock tbInfo)
{
    try {
        const std::string tableName = arrayToString(tbInfo.getName());
        const std::string dbName = inferDbNameFromTableBlock(tbInfo);
        if (tableName.empty() || dbName.empty()) {
            LogWriter::warning("storage",
                               "DatabaseManager",
                               "createTable",
                               "Rejected createTable(TableBlock) with empty table or database name.");
            return false;
        }
        LogWriter::info("storage",
                        "DatabaseManager",
                        "createTable",
                        std::string("Creating table from TableBlock: ") + dbName + "." + tableName);
        if (!createTable(dbName, tableName, buildColumns(tbInfo))) {
            return false;
        }
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        TableBlock normalized = normalizeTableBlock(dbPath, tableName, tbInfo);
        normalized.setRecordNum(0);
        normalized.setModifyTime(buildCurrentDateTime());
        return upsertTableBlock(dbPath, tableName, normalized);
    } catch (...) {
        LogWriter::error("storage", "DatabaseManager", "createTable", "Unknown exception in createTable(TableBlock).");
        return false;
    }
}

bool DatabaseManager::createTable(const std::string &dbName,
                                  const std::string &tableName,
                                  const std::vector<std::string> &columns,
                                  const std::vector<storage::ColumnMeta> &columnMetas)
{
    if (dbName.empty() || tableName.empty() || columns.empty()) {
        LogWriter::warning("storage",
                           "DatabaseManager",
                           "createTable",
                           "Rejected createTable with empty database name, table name, or columns.");
        return false;
    }

    try {
        const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
        const auto dbPath = dbRootPath / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("storage",
                               "DatabaseManager",
                               "createTable",
                               std::string("Database directory not found: ") + dbName);
            return false;
        }
        storage::Table::create(dbPath, tableName, columns, columnMetas);
        TableBlock block = buildTableBlock(dbPath, tableName);
        block.setFieldNum(static_cast<std::int32_t>(columns.size()));
        block.setRecordNum(0);
        block.setModifyTime(buildCurrentDateTime());
        if (!upsertTableBlock(dbPath, tableName, block)) {
            LogWriter::error("storage",
                             "DatabaseManager",
                             "createTable",
                             std::string("Failed to update table descriptor file for ") + dbName + "." + tableName);
            return false;
        }
        LogWriter::info("storage",
                        "DatabaseManager",
                        "createTable",
                        std::string("Table created successfully: ") + dbName + "." + tableName);
        return true;
    } catch (...) {
        LogWriter::error("storage",
                         "DatabaseManager",
                         "createTable",
                         std::string("Unknown exception while creating table: ") + dbName + "." + tableName);
        return false;
    }
}

bool DatabaseManager::createTable(const std::string &dbName,
                                  const std::string &tableName,
                                  const std::vector<storage::Table::ColumnDefinition> &columns)
{
    if (dbName.empty() || tableName.empty() || columns.empty()) {
        return false;
    }
    try {
        std::vector<std::string> names;
        std::vector<storage::ColumnMeta> columnMetas;
        names.reserve(columns.size());
        columnMetas.reserve(columns.size());
        for (const auto &column : columns) {
            names.push_back(column.name);
            storage::ColumnMeta meta;
            if (column.constraints.notNull) {
                meta.integrities |= 1;
            }
            if (column.constraints.unique) {
                meta.integrities |= 4;
            }
            if (column.constraints.hasDefault) {
                meta.defaultValue = column.constraints.defaultValue;
            }
            columnMetas.push_back(std::move(meta));
        }
        return createTable(dbName, tableName, names, columnMetas);
    } catch (...) {
        return false;
    }
}

bool DatabaseManager::insertRow(const std::string &dbName,
                                const std::string &tableName,
                                const std::vector<std::string> &values)
{
    if (dbName.empty() || tableName.empty() || values.empty()) {
        LogWriter::warning("storage",
                           "DatabaseManager",
                           "insertRow",
                           "Rejected insertRow with empty database name, table name, or values.");
        return false;
    }

    try {
        const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
        const auto dbPath = dbRootPath / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("storage",
                               "DatabaseManager",
                               "insertRow",
                               std::string("Database directory not found: ") + dbName);
            return false;
        }

        auto table = storage::Table::load(dbPath, tableName);
        table.insert(values);
        TableBlock block;
        if (readTableBlock(dbPath, tableName, block)) {
            block.setRecordNum(std::max(0, block.getRecordNum()) + 1);
            block.setModifyTime(buildCurrentDateTime());
            if (!upsertTableBlock(dbPath, tableName, block)) {
                LogWriter::warning("storage",
                                   "DatabaseManager",
                                   "insertRow",
                                   std::string("Failed to update .tb metadata for ") + dbName + "." + tableName);
            }
        }
        LogWriter::info("storage",
                        "DatabaseManager",
                        "insertRow",
                        std::string("Inserted row into ") + dbName + "." + tableName
                            + ", value count=" + std::to_string(values.size()));
        return true;
    } catch (...) {
        LogWriter::error("storage",
                         "DatabaseManager",
                         "insertRow",
                         std::string("Unknown exception while inserting row into ") + dbName + "." + tableName);
        return false;
    }
}

bool DatabaseManager::updateRowByPrimaryKey(const std::string &dbName,
                                            const std::string &tableName,
                                            const std::string &primaryKey,
                                            const std::vector<std::string> &newValues)
{
    if (dbName.empty() || tableName.empty() || primaryKey.empty() || newValues.empty()) {
        LogWriter::warning("storage",
                           "DatabaseManager",
                           "updateRowByPrimaryKey",
                           "Rejected update with empty database name, table name, primary key, or values.");
        return false;
    }

    try {
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("storage",
                               "DatabaseManager",
                               "updateRowByPrimaryKey",
                               std::string("Database directory not found: ") + dbName);
            return false;
        }

        auto table = storage::Table::load(dbPath, tableName);
        const bool updated = table.updateByPrimaryKey(primaryKey, newValues);
        if (updated) {
            TableBlock block;
            if (readTableBlock(dbPath, tableName, block)) {
                block.setModifyTime(buildCurrentDateTime());
                if (!upsertTableBlock(dbPath, tableName, block)) {
                    LogWriter::warning("storage",
                                       "DatabaseManager",
                                       "updateRowByPrimaryKey",
                                       std::string("Failed to update .tb metadata for ") + dbName + "." + tableName);
                }
            }
        }
        LogWriter::info("storage",
                        "DatabaseManager",
                        "updateRowByPrimaryKey",
                        std::string("Update row result for ") + dbName + "." + tableName + ", key=" + primaryKey
                            + ", updated=" + (updated ? "true" : "false"));
        return updated;
    } catch (...) {
        LogWriter::error("storage",
                         "DatabaseManager",
                         "updateRowByPrimaryKey",
                         std::string("Unknown exception while updating row in ") + dbName + "." + tableName);
        return false;
    }
}

bool DatabaseManager::deleteRowByPrimaryKey(const std::string &dbName,
                                            const std::string &tableName,
                                            const std::string &primaryKey)
{
    if (dbName.empty() || tableName.empty() || primaryKey.empty()) {
        LogWriter::warning("storage",
                           "DatabaseManager",
                           "deleteRowByPrimaryKey",
                           "Rejected delete with empty database name, table name, or primary key.");
        return false;
    }

    try {
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("storage",
                               "DatabaseManager",
                               "deleteRowByPrimaryKey",
                               std::string("Database directory not found: ") + dbName);
            return false;
        }

        auto table = storage::Table::load(dbPath, tableName);
        const bool deleted = table.deleteByPrimaryKey(primaryKey);
        if (deleted) {
            TableBlock block;
            if (readTableBlock(dbPath, tableName, block)) {
                block.setRecordNum(std::max(0, block.getRecordNum() - 1));
                block.setModifyTime(buildCurrentDateTime());
                if (!upsertTableBlock(dbPath, tableName, block)) {
                    LogWriter::warning("storage",
                                       "DatabaseManager",
                                       "deleteRowByPrimaryKey",
                                       std::string("Failed to update .tb metadata for ") + dbName + "." + tableName);
                }
            }
        }
        LogWriter::info("storage",
                        "DatabaseManager",
                        "deleteRowByPrimaryKey",
                        std::string("Delete row result for ") + dbName + "." + tableName + ", key=" + primaryKey
                            + ", deleted=" + (deleted ? "true" : "false"));
        return deleted;
    } catch (...) {
        LogWriter::error("storage",
                         "DatabaseManager",
                         "deleteRowByPrimaryKey",
                         std::string("Unknown exception while deleting row in ") + dbName + "." + tableName);
        return false;
    }
}

bool DatabaseManager::addColumnConstraint(const std::string &dbName,
                                          const std::string &tableName,
                                          const storage::Table::ColumnConstraintSpec &constraint)
{
    if (dbName.empty() || tableName.empty() || constraint.column.empty()) {
        return false;
    }
    try {
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            return false;
        }
        auto table = storage::Table::load(dbPath, tableName);
        return table.addColumnConstraint(constraint);
    } catch (...) {
        return false;
    }
}

std::vector<storage::Row> DatabaseManager::selectRows(
    const std::string &dbName,
    const std::string &tableName,
    const std::vector<std::string> &targetColumns,
    const std::vector<storage::Table::WhereCondition> &whereConditions,
    const std::vector<storage::Table::QueryConstraint> &queryConstraints,
    const storage::Table::SelectOptions &options)
{
    if (dbName.empty() || tableName.empty()) {
        return {};
    }
    try {
        const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            return {};
        }
        auto table = storage::Table::load(dbPath, tableName);
        return table.select(targetColumns, whereConditions, queryConstraints, options);
    } catch (...) {
        return {};
    }
}

bool DatabaseManager::dropTable(std::string tableName)
{
    if (tableName.empty()) {
        LogWriter::warning("storage", "DatabaseManager", "dropTable", "Rejected empty table name.");
        return false;
    }

    const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        LogWriter::warning("storage", "DatabaseManager", "dropTable", "Database root directory not found.");
        return false;
    }

    bool removedAny = false;
    bool descriptorUpdated = true;
    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_directory()) {
            continue;
        }

        const auto dbPath = entry.path();
        const std::vector<std::filesystem::path> candidates = {
            dbPath / (tableName + ".tdf"),
            dbPath / (tableName + ".trd"),
            dbPath / (tableName + ".tic"),
            dbPath / (tableName + ".tid")};
        for (const auto &candidate : candidates) {
            if (std::filesystem::exists(candidate)) {
                removedAny = std::filesystem::remove(candidate) || removedAny;
            }
        }
        if (std::filesystem::exists(candidates.front())) {
            continue;
        }
        descriptorUpdated = removeTableBlock(dbPath, tableName) && descriptorUpdated;
    }
    if (!descriptorUpdated) {
        LogWriter::error("storage",
                         "DatabaseManager",
                         "dropTable",
                         std::string("Failed to update .tb descriptor while dropping ") + tableName);
        return false;
    }
    LogWriter::info("storage",
                    "DatabaseManager",
                    "dropTable",
                    std::string("Drop table result for ") + tableName + ": " + (removedAny ? "success" : "not found"));
    return removedAny;
}

bool DatabaseManager::modifyTable(std::string tableName, TableBlock newTbInfo)
{
    if (tableName.empty()) {
        return false;
    }
    const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        return false;
    }
    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto dbPath = entry.path();
        TableBlock current;
        if (!readTableBlock(dbPath, tableName, current)) {
            continue;
        }
        TableBlock merged = current;
        if (newTbInfo.getFieldNum() > 0) {
            merged.setFieldNum(newTbInfo.getFieldNum());
        }
        if (newTbInfo.getRecordNum() >= 0) {
            merged.setRecordNum(newTbInfo.getRecordNum());
        }
        if (!arrayToString(newTbInfo.getTdf()).empty()) merged.setTdf(newTbInfo.getTdf());
        if (!arrayToString(newTbInfo.getTic()).empty()) merged.setTic(newTbInfo.getTic());
        if (!arrayToString(newTbInfo.getTrd()).empty()) merged.setTrd(newTbInfo.getTrd());
        if (!arrayToString(newTbInfo.getTid()).empty()) merged.setTid(newTbInfo.getTid());
        if (newTbInfo.getCreateTime().getYear() > 0) merged.setCreateTime(newTbInfo.getCreateTime());
        merged.setModifyTime(newTbInfo.getModifyTime().getYear() > 0
                                 ? newTbInfo.getModifyTime()
                                 : buildCurrentDateTime());
        return upsertTableBlock(dbPath, tableName, merged);
    }
    return false;
}

TableBlock DatabaseManager::getTableInfo(std::string tableName)
{
    if (tableName.empty()) {
        LogWriter::warning("storage", "DatabaseManager", "getTableInfo", "Rejected empty table name.");
        return {};
    }

    const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        LogWriter::warning("storage", "DatabaseManager", "getTableInfo", "Database root directory not found.");
        return {};
    }

    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto dbPath = entry.path();
        if (std::filesystem::exists(dbPath / (tableName + ".tdf"))) {
            LogWriter::debug("storage",
                             "DatabaseManager",
                             "getTableInfo",
                             std::string("Found table info for ") + tableName + " in " + dbPath.filename().string());
            TableBlock block;
            if (readTableBlock(dbPath, tableName, block)) {
                return block;
            }
            return buildTableBlock(dbPath, tableName);
        }
    }
    LogWriter::debug("storage", "DatabaseManager", "getTableInfo", std::string("Table info not found for ") + tableName);
    return {};
}

std::vector<TableBlock> DatabaseManager::getAllTables()
{
    std::vector<TableBlock> blocks;
    const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        LogWriter::warning("storage", "DatabaseManager", "getAllTables", "Database root directory not found.");
        return blocks;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto dbPath = entry.path();
        const auto descriptorBlocks = readTableBlocksFromDescriptor(dbPath);
        if (!descriptorBlocks.empty()) {
            blocks.insert(blocks.end(), descriptorBlocks.begin(), descriptorBlocks.end());
            continue;
        }
        for (const auto &dbFile : std::filesystem::directory_iterator(dbPath)) {
            if (dbFile.is_regular_file() && dbFile.path().extension() == ".tdf") {
                blocks.push_back(buildTableBlock(dbPath, dbFile.path().stem().string()));
            }
        }
    }
    LogWriter::debug("storage",
                     "DatabaseManager",
                      "getAllTables",
                      std::string("Enumerated table count=") + std::to_string(blocks.size()));
    return blocks;
}

std::vector<TableBlock> DatabaseManager::getAllTablesForDb(const std::string &dbName)
{
    std::vector<TableBlock> blocks;
    if (dbName.empty()) {
        return blocks;
    }

    const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
    const auto dbPath = dbRootPath / dbName;
    if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
        return blocks;
    }

    const auto descriptorBlocks = readTableBlocksFromDescriptor(dbPath);
    if (!descriptorBlocks.empty()) {
        return descriptorBlocks;
    }

    for (const auto &dbFile : std::filesystem::directory_iterator(dbPath)) {
        if (dbFile.is_regular_file() && dbFile.path().extension() == ".tdf") {
            blocks.push_back(buildTableBlock(dbPath, dbFile.path().stem().string()));
        }
    }

    LogWriter::debug("storage",
                     "DatabaseManager",
                     "getAllTablesForDb",
                     std::string("Enumerated table count=") + std::to_string(blocks.size())
                         + " for database " + dbName);
    return blocks;
}

std::vector<std::string> DatabaseManager::getTableColumns(const std::string &dbName,
                                                           const std::string &tableName)
{
    if (dbName.empty() || tableName.empty()) {
        return {};
    }

    const auto &dbRootPath = SystemCatalogManager::getDataRootPath();
    const auto tdfPath = dbRootPath / dbName / (tableName + ".tdf");
    if (!std::filesystem::exists(tdfPath)) {
        return {};
    }

    std::ifstream ifs(tdfPath);
    if (!ifs.good()) {
        return {};
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.rfind("columns=", 0) == 0) {
            const std::string columnsStr = line.substr(8);
            std::vector<std::string> columns;
            std::stringstream ss(columnsStr);
            std::string item;
            while (std::getline(ss, item, '|')) {
                const auto colonPos = item.find(':');
                columns.push_back(colonPos == std::string::npos ? item : item.substr(0, colonPos));
            }
            return columns;
        }
    }

    return {};
}
