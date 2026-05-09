#include "LogBlock.h"

#include <stdexcept>

// ──────────────────────────────────────────────
// 构造
// ──────────────────────────────────────────────

LogBlock::LogBlock()
    : operationId(0),
      timestamp(),
      databaseName(),
      tableName(),
      operationType(DbLogOperationType::Insert),
      beforeData(),
      afterData(),
      sqlText()
{
}

// ──────────────────────────────────────────────
// Getter
// ──────────────────────────────────────────────

std::int64_t LogBlock::getOperationId() const
{
    return operationId;
}

const DateTime &LogBlock::getTimestamp() const
{
    return timestamp;
}

const std::string &LogBlock::getDatabaseName() const
{
    return databaseName;
}

const std::string &LogBlock::getTableName() const
{
    return tableName;
}

DbLogOperationType LogBlock::getOperationType() const
{
    return operationType;
}

const std::string &LogBlock::getBeforeData() const
{
    return beforeData;
}

const std::string &LogBlock::getAfterData() const
{
    return afterData;
}

const std::string &LogBlock::getSqlText() const
{
    return sqlText;
}

// ──────────────────────────────────────────────
// Setter
// ──────────────────────────────────────────────

void LogBlock::setOperationId(std::int64_t operationId)
{
    this->operationId = operationId;
}

void LogBlock::setTimestamp(const DateTime &timestamp)
{
    this->timestamp = timestamp;
}

void LogBlock::setDatabaseName(const std::string &databaseName)
{
    this->databaseName = databaseName;
}

void LogBlock::setTableName(const std::string &tableName)
{
    this->tableName = tableName;
}

void LogBlock::setOperationType(DbLogOperationType operationType)
{
    this->operationType = operationType;
}

void LogBlock::setBeforeData(const std::string &beforeData)
{
    this->beforeData = beforeData;
}

void LogBlock::setAfterData(const std::string &afterData)
{
    this->afterData = afterData;
}

void LogBlock::setSqlText(const std::string &sqlText)
{
    this->sqlText = sqlText;
}

// ──────────────────────────────────────────────
// JSON 序列化 / 反序列化
// ──────────────────────────────────────────────

nlohmann::json LogBlock::toJson() const
{
    nlohmann::json j;
    j["operation_id"] = operationId;
    j["timestamp"] = dateTimeToJson(timestamp);
    j["database_name"] = databaseName;
    j["table_name"] = tableName;
    j["operation_type"] = operationTypeToString(operationType);
    j["before_data"] = beforeData;
    j["after_data"] = afterData;
    j["sql_text"] = sqlText;
    return j;
}

bool LogBlock::fromJson(const nlohmann::json &jsonData, LogBlock &outBlock)
{
    try {
        if (!jsonData.contains("operation_id") || !jsonData.contains("timestamp")
            || !jsonData.contains("database_name") || !jsonData.contains("operation_type")) {
            return false;
        }

        LogBlock block;
        block.setOperationId(jsonData["operation_id"].get<std::int64_t>());

        DateTime ts;
        if (!jsonToDateTime(jsonData["timestamp"], ts)) {
            return false;
        }
        block.setTimestamp(ts);

        block.setDatabaseName(jsonData.value("database_name", ""));
        block.setTableName(jsonData.value("table_name", ""));
        block.setOperationType(stringToOperationType(jsonData.value("operation_type", "Insert")));
        block.setBeforeData(jsonData.value("before_data", ""));
        block.setAfterData(jsonData.value("after_data", ""));
        block.setSqlText(jsonData.value("sql_text", ""));

        outBlock = block;
        return true;
    } catch (...) {
        return false;
    }
}

std::string LogBlock::toJsonString() const
{
    return toJson().dump();
}

bool LogBlock::fromJsonString(const std::string &jsonStr, LogBlock &outBlock)
{
    try {
        nlohmann::json j = nlohmann::json::parse(jsonStr);
        return fromJson(j, outBlock);
    } catch (...) {
        return false;
    }
}

// ──────────────────────────────────────────────
// DateTime ↔ JSON 辅助
// ──────────────────────────────────────────────

nlohmann::json LogBlock::dateTimeToJson(const DateTime &dateTime)
{
    nlohmann::json j;
    j["year"] = dateTime.getYear();
    j["month"] = dateTime.getMonth();
    j["day_of_week"] = dateTime.getDayOfWeek();
    j["day"] = dateTime.getDay();
    j["hour"] = dateTime.getHour();
    j["minute"] = dateTime.getMinute();
    j["second"] = dateTime.getSecond();
    j["milliseconds"] = dateTime.getMilliseconds();
    return j;
}

bool LogBlock::jsonToDateTime(const nlohmann::json &jsonData, DateTime &outDateTime)
{
    try {
        if (!jsonData.contains("year") || !jsonData.contains("month") || !jsonData.contains("day")) {
            return false;
        }
        outDateTime.setYear(static_cast<std::uint16_t>(jsonData["year"].get<int>()));
        outDateTime.setMonth(static_cast<std::uint16_t>(jsonData["month"].get<int>()));
        outDateTime.setDayOfWeek(static_cast<std::uint16_t>(jsonData.value("day_of_week", 1)));
        outDateTime.setDay(static_cast<std::uint16_t>(jsonData["day"].get<int>()));
        outDateTime.setHour(static_cast<std::uint16_t>(jsonData.value("hour", 0)));
        outDateTime.setMinute(static_cast<std::uint16_t>(jsonData.value("minute", 0)));
        outDateTime.setSecond(static_cast<std::uint16_t>(jsonData.value("second", 0)));
        outDateTime.setMilliseconds(static_cast<std::uint16_t>(jsonData.value("milliseconds", 0)));
        return true;
    } catch (...) {
        return false;
    }
}

// ──────────────────────────────────────────────
// 操作类型 ↔ 字符串
// ──────────────────────────────────────────────

std::string LogBlock::operationTypeToString(DbLogOperationType opType)
{
    switch (opType) {
    case DbLogOperationType::CreateDatabase:
        return "CreateDatabase";
    case DbLogOperationType::DropDatabase:
        return "DropDatabase";
    case DbLogOperationType::CreateTable:
        return "CreateTable";
    case DbLogOperationType::DropTable:
        return "DropTable";
    case DbLogOperationType::AlterTable:
        return "AlterTable";
    case DbLogOperationType::Insert:
        return "Insert";
    case DbLogOperationType::Delete:
        return "Delete";
    case DbLogOperationType::Update:
        return "Update";
    default:
        return "Unknown";
    }
}

DbLogOperationType LogBlock::stringToOperationType(const std::string &str)
{
    if (str == "CreateDatabase") {
        return DbLogOperationType::CreateDatabase;
    } else if (str == "DropDatabase") {
        return DbLogOperationType::DropDatabase;
    } else if (str == "CreateTable") {
        return DbLogOperationType::CreateTable;
    } else if (str == "DropTable") {
        return DbLogOperationType::DropTable;
    } else if (str == "AlterTable") {
        return DbLogOperationType::AlterTable;
    } else if (str == "Insert") {
        return DbLogOperationType::Insert;
    } else if (str == "Delete") {
        return DbLogOperationType::Delete;
    } else if (str == "Update") {
        return DbLogOperationType::Update;
    }
    return DbLogOperationType::Insert; // 默认回退
}
