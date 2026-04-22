#include "SqlData.h"

#include <nlohmann/json.hpp>

SqlData::SqlData()
    : dbVersion(0)
{
}

SqlData::SqlData(const std::string &userID,
                 const std::string &dbName,
                 std::uint64_t dbVersion,
                 const std::string &sql)
    : userID(userID), dbName(dbName), dbVersion(dbVersion), sql(sql)
{
}

std::string SqlData::toJson() const
{
    nlohmann::json jsonObject;
    jsonObject["userID"] = userID;
    jsonObject["dbName"] = dbName;
    jsonObject["dbVersion"] = dbVersion;
    jsonObject["sql"] = sql;
    return jsonObject.dump();
}

SqlData SqlData::fromJson(const std::string &jsonStr)
{
    const nlohmann::json jsonObject = nlohmann::json::parse(jsonStr);
    return SqlData(jsonObject.value("userID", ""),
                   jsonObject.value("dbName", ""),
                   jsonObject.value("dbVersion", static_cast<std::uint64_t>(0)),
                   jsonObject.value("sql", ""));
}

const std::string &SqlData::getUserID() const
{
    return userID;
}

void SqlData::setUserID(const std::string &userID)
{
    this->userID = userID;
}

const std::string &SqlData::getDbName() const
{
    return dbName;
}

void SqlData::setDbName(const std::string &dbName)
{
    this->dbName = dbName;
}

std::uint64_t SqlData::getDbVersion() const
{
    return dbVersion;
}

void SqlData::setDbVersion(std::uint64_t dbVersion)
{
    this->dbVersion = dbVersion;
}

const std::string &SqlData::getSql() const
{
    return sql;
}

void SqlData::setSql(const std::string &sql)
{
    this->sql = sql;
}
