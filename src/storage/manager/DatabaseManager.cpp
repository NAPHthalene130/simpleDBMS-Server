#include "DatabaseManager.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "storage/object/Table.h"

namespace {

constexpr const char *kDbRootPath = "data";

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
            return false;
        }
        return createTable(dbName, tableName, buildColumns(tbInfo));
    } catch (...) {
        return false;
    }
}

bool DatabaseManager::createTable(const std::string &dbName,
                                  const std::string &tableName,
                                  const std::vector<std::string> &columns)
{
    if (dbName.empty() || tableName.empty() || columns.empty()) {
        return false;
    }

    try {
        const auto dbPath = std::filesystem::path(kDbRootPath) / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            return false;
        }
        storage::Table::create(dbPath, tableName, columns);
        return true;
    } catch (...) {
        return false;
    }
}

bool DatabaseManager::insertRow(const std::string &dbName,
                                const std::string &tableName,
                                const std::vector<std::string> &values)
{
    if (dbName.empty() || tableName.empty() || values.empty()) {
        return false;
    }

    try {
        const auto dbPath = std::filesystem::path(kDbRootPath) / dbName;
        if (!std::filesystem::exists(dbPath) || !std::filesystem::is_directory(dbPath)) {
            return false;
        }

        auto table = storage::Table::load(dbPath, tableName);
        table.insert(values);
        return true;
    } catch (...) {
        return false;
    }
}

bool DatabaseManager::dropTable(std::string tableName)
{
    if (tableName.empty()) {
        return false;
    }

    const auto dbRootPath = std::filesystem::path(kDbRootPath);
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
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
        return {};
    }

    const auto dbRootPath = std::filesystem::path(kDbRootPath);
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
        return {};
    }

    for (const auto &entry : std::filesystem::directory_iterator(dbRootPath)) {
        if (!entry.is_directory()) {
            continue;
        }
        const auto dbPath = entry.path();
        if (std::filesystem::exists(dbPath / (tableName + ".tdf"))) {
            return buildTableBlock(dbPath, tableName);
        }
    }
    return {};
}

std::vector<TableBlock> DatabaseManager::getAllTables()
{
    std::vector<TableBlock> blocks;
    const auto dbRootPath = std::filesystem::path(kDbRootPath);
    if (!std::filesystem::exists(dbRootPath) || !std::filesystem::is_directory(dbRootPath)) {
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
    return blocks;
}
