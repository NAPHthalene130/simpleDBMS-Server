#include "DatabaseManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "log/LogWriter.h"
#include "storage/object/Table.h"

namespace {

/**
 * @brief 获取数据存储根目录的绝对路径
 * @author NAPH130
 * @return 数据根目录路径（基于源文件位置，不依赖进程工作目录）
 * @details 本文件位于 src/storage/manager/，数据目录期望在 src/storage/data/。
 *          通过 __FILE__ 向上两级获得 storage/ 再拼接 data/，确保无论从何处启动服务端都写入正确位置。
 */
const std::filesystem::path &getDataRootPath()
{
    static const std::filesystem::path dataRoot =
        (std::filesystem::path(__FILE__).parent_path().parent_path() / "data").lexically_normal();
    return dataRoot;
}

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
    block.setTdf(stringToArray<256>((dbPath / (tableName + ".tdf")).string()));
    block.setTrd(stringToArray<256>((dbPath / (tableName + ".trd")).string()));
    block.setTic(stringToArray<256>((dbPath / (tableName + ".tic")).string()));
    block.setTid(stringToArray<256>((dbPath / (tableName + ".tid")).string()));
    return block;
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
        return createTable(dbName, tableName, buildColumns(tbInfo));
    } catch (...) {
        LogWriter::error("storage", "DatabaseManager", "createTable", "Unknown exception in createTable(TableBlock).");
        return false;
    }
}

bool DatabaseManager::createTable(const std::string &dbName,
                                  const std::string &tableName,
                                  const std::vector<std::string> &columns)
{
    if (dbName.empty() || tableName.empty() || columns.empty()) {
        LogWriter::warning("storage",
                           "DatabaseManager",
                           "createTable",
                           "Rejected createTable with empty database name, table name, or columns.");
        return false;
    }

    try {
        const auto &dbRootPath = getDataRootPath();
        const auto dbPath = dbRootPath / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            LogWriter::warning("storage",
                               "DatabaseManager",
                               "createTable",
                               std::string("Database directory not found: ") + dbName);
            return false;
        }
        storage::Table::create(dbPath, tableName, columns);
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
        const auto &dbRootPath = getDataRootPath();
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

bool DatabaseManager::dropTable(std::string tableName)
{
    if (tableName.empty()) {
        LogWriter::warning("storage", "DatabaseManager", "dropTable", "Rejected empty table name.");
        return false;
    }

    const auto &dbRootPath = getDataRootPath();
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        LogWriter::warning("storage", "DatabaseManager", "dropTable", "Database root directory not found.");
        return false;
    }

    bool removedAny = false;
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
    }
    LogWriter::info("storage",
                    "DatabaseManager",
                    "dropTable",
                    std::string("Drop table result for ") + tableName + ": " + (removedAny ? "success" : "not found"));
    return removedAny;
}

bool DatabaseManager::modifyTable(std::string tableName, TableBlock newTbInfo)
{
    (void)tableName;
    (void)newTbInfo;
    return true;
}

TableBlock DatabaseManager::getTableInfo(std::string tableName)
{
    if (tableName.empty()) {
        LogWriter::warning("storage", "DatabaseManager", "getTableInfo", "Rejected empty table name.");
        return {};
    }

    const auto &dbRootPath = getDataRootPath();
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
            return buildTableBlock(dbPath, tableName);
        }
    }
    LogWriter::debug("storage", "DatabaseManager", "getTableInfo", std::string("Table info not found for ") + tableName);
    return {};
}

std::vector<TableBlock> DatabaseManager::getAllTables()
{
    std::vector<TableBlock> blocks;
    const auto &dbRootPath = getDataRootPath();
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        LogWriter::warning("storage", "DatabaseManager", "getAllTables", "Database root directory not found.");
        return blocks;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto dbPath = entry.path();
        for (const auto &dbFile : std::filesystem::directory_iterator(dbPath)) {
            if (!dbFile.is_regular_file() || dbFile.path().extension() != ".tdf") {
                continue;
            }
            blocks.push_back(buildTableBlock(dbPath, dbFile.path().stem().string()));
        }
    }
    LogWriter::debug("storage",
                     "DatabaseManager",
                     "getAllTables",
                     std::string("Enumerated table count=") + std::to_string(blocks.size()));
    return blocks;
}
