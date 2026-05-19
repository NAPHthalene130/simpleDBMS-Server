#pragma once

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "storage/manager/DatabaseManager.h"
#include "storage/manager/SystemCatalogManager.h"
#include "storage/object/Table.h"

namespace dblog_snapshot {

template <std::size_t N>
inline std::string fixedArrayToString(const std::array<char, N> &value)
{
    const auto endIt = std::find(value.begin(), value.end(), '\0');
    return std::string(value.begin(), endIt);
}

inline std::string dataTypeToString(storage::DataType dataType)
{
    switch (dataType) {
    case storage::DataType::INT:
        return "INT";
    case storage::DataType::FLOAT:
        return "FLOAT";
    case storage::DataType::VARCHAR:
        return "VARCHAR";
    case storage::DataType::TEXT:
        return "TEXT";
    default:
        return "TEXT";
    }
}

inline storage::DataType stringToDataType(const std::string &typeName)
{
    if (typeName == "INT") {
        return storage::DataType::INT;
    }
    if (typeName == "FLOAT") {
        return storage::DataType::FLOAT;
    }
    if (typeName == "VARCHAR") {
        return storage::DataType::VARCHAR;
    }
    return storage::DataType::TEXT;
}

inline nlohmann::json columnMetaToJson(const storage::ColumnMeta &meta)
{
    nlohmann::json jsonMeta;
    jsonMeta["data_type"] = dataTypeToString(meta.dataType);
    jsonMeta["varchar_len"] = meta.varcharLen;
    jsonMeta["integrities"] = meta.integrities;
    jsonMeta["default_value"] = meta.defaultValue;
    return jsonMeta;
}

inline storage::ColumnMeta jsonToColumnMeta(const nlohmann::json &jsonMeta)
{
    storage::ColumnMeta meta;
    meta.dataType = stringToDataType(jsonMeta.value("data_type", "TEXT"));
    meta.varcharLen = static_cast<std::uint16_t>(jsonMeta.value("varchar_len", 0));
    meta.integrities = jsonMeta.value("integrities", 0);
    meta.defaultValue = jsonMeta.value("default_value", "");
    return meta;
}

inline nlohmann::json buildTableSnapshot(const std::filesystem::path &dbPath,
                                         const std::string &tableName)
{
    auto table = storage::Table::load(dbPath, tableName);
    const auto &schema = table.schema();
    const auto rows = table.select({}, {});

    nlohmann::json snapshot;
    snapshot["table_name"] = tableName;
    snapshot["columns"] = schema.columns;

    nlohmann::json metaArray = nlohmann::json::array();
    for (const auto &meta : schema.columnMetas) {
        metaArray.push_back(columnMetaToJson(meta));
    }
    snapshot["column_metas"] = metaArray;

    nlohmann::json rowArray = nlohmann::json::array();
    for (const auto &row : rows) {
        rowArray.push_back(row.values);
    }
    snapshot["rows"] = rowArray;
    return snapshot;
}

inline nlohmann::json buildDatabaseSnapshot(DatabaseManager *databaseManager,
                                            const std::string &dbName)
{
    nlohmann::json snapshot;
    snapshot["database_name"] = dbName;
    snapshot["tables"] = nlohmann::json::array();

    if (databaseManager == nullptr || dbName.empty()) {
        return snapshot;
    }

    const auto dbPath = SystemCatalogManager::getDataRootPath() / dbName;
    for (const auto &tableBlock : databaseManager->getAllTablesForDb(dbName)) {
        const std::string tableName = fixedArrayToString(tableBlock.getName());
        if (tableName.empty()) {
            continue;
        }
        snapshot["tables"].push_back(buildTableSnapshot(dbPath, tableName));
    }

    return snapshot;
}

inline std::vector<std::string> parseColumns(const nlohmann::json &snapshot)
{
    std::vector<std::string> columns;
    if (!snapshot.contains("columns") || !snapshot["columns"].is_array()) {
        return columns;
    }
    for (const auto &item : snapshot["columns"]) {
        columns.push_back(item.get<std::string>());
    }
    return columns;
}

inline std::vector<storage::ColumnMeta> parseColumnMetas(const nlohmann::json &snapshot)
{
    std::vector<storage::ColumnMeta> metas;
    if (!snapshot.contains("column_metas") || !snapshot["column_metas"].is_array()) {
        return metas;
    }
    for (const auto &item : snapshot["column_metas"]) {
        metas.push_back(jsonToColumnMeta(item));
    }
    return metas;
}

inline std::vector<std::vector<std::string>> parseRows(const nlohmann::json &snapshot)
{
    std::vector<std::vector<std::string>> rows;
    if (!snapshot.contains("rows") || !snapshot["rows"].is_array()) {
        return rows;
    }
    for (const auto &rowJson : snapshot["rows"]) {
        if (!rowJson.is_array()) {
            continue;
        }
        std::vector<std::string> rowValues;
        for (const auto &value : rowJson) {
            rowValues.push_back(value.get<std::string>());
        }
        rows.push_back(std::move(rowValues));
    }
    return rows;
}

} // namespace dblog_snapshot
