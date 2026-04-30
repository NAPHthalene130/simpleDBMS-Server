#include "NetworkTransferData.h"

#include <nlohmann/json.hpp>

TableNode::TableNode() = default;

TableNode::TableNode(const std::string &name, const std::vector<std::string> &fields)
    : name(name), fields(fields)
{
}

const std::string &TableNode::getName() const
{
    return name;
}

void TableNode::setName(const std::string &name)
{
    this->name = name;
}

const std::vector<std::string> &TableNode::getFields() const
{
    return fields;
}

void TableNode::setFields(const std::vector<std::string> &fields)
{
    this->fields = fields;
}

std::string TableNode::toJson() const
{
    nlohmann::json jsonObject;
    jsonObject["name"] = name;
    jsonObject["fields"] = fields;
    return jsonObject.dump();
}

TableNode TableNode::fromJson(const std::string &jsonStr)
{
    const nlohmann::json jsonObject = nlohmann::json::parse(jsonStr);
    return TableNode(jsonObject.value("name", ""),
                     jsonObject.value("fields", std::vector<std::string> {}));
}

DatabaseNode::DatabaseNode() = default;

DatabaseNode::DatabaseNode(const std::string &name, const std::vector<TableNode> &tables)
    : name(name), tables(tables)
{
}

const std::string &DatabaseNode::getName() const
{
    return name;
}

void DatabaseNode::setName(const std::string &name)
{
    this->name = name;
}

const std::vector<TableNode> &DatabaseNode::getTables() const
{
    return tables;
}

void DatabaseNode::setTables(const std::vector<TableNode> &tables)
{
    this->tables = tables;
}

std::string DatabaseNode::toJson() const
{
    nlohmann::json jsonObject;
    jsonObject["name"] = name;

    nlohmann::json tablesJson = nlohmann::json::array();
    for (const TableNode &tableNode : tables) {
        tablesJson.push_back(nlohmann::json::parse(tableNode.toJson()));
    }
    jsonObject["tables"] = tablesJson;
    return jsonObject.dump();
}

DatabaseNode DatabaseNode::fromJson(const std::string &jsonStr)
{
    const nlohmann::json jsonObject = nlohmann::json::parse(jsonStr);

    std::vector<TableNode> tableNodes;
    if (jsonObject.contains("tables") && jsonObject["tables"].is_array()) {
        for (const nlohmann::json &tableJson : jsonObject["tables"]) {
            tableNodes.push_back(TableNode::fromJson(tableJson.dump()));
        }
    }

    return DatabaseNode(jsonObject.value("name", ""), tableNodes);
}

NetworkTransferData::NetworkTransferData()
    : success(false),
      affectedRows(0)
{
}

NetworkTransferData::NetworkTransferData(const std::string &type, const std::string &id)
    : type(type),
      id(id),
      success(false),
      affectedRows(0)
{
}

std::string NetworkTransferData::toJson() const
{
    nlohmann::json jsonObject;
    jsonObject["type"] = type;
    jsonObject["id"] = id;
    jsonObject["password"] = password;
    jsonObject["dbName"] = dbName;
    jsonObject["sql"] = sql;
    jsonObject["success"] = success;
    jsonObject["message"] = message;
    jsonObject["affectedRows"] = affectedRows;
    jsonObject["columns"] = columns;
    jsonObject["rows"] = rows;

    nlohmann::json databasesJson = nlohmann::json::array();
    for (const DatabaseNode &databaseNode : databases) {
        databasesJson.push_back(nlohmann::json::parse(databaseNode.toJson()));
    }
    jsonObject["databases"] = databasesJson;

    return jsonObject.dump();
}

NetworkTransferData NetworkTransferData::fromJson(const std::string &jsonStr)
{
    const nlohmann::json jsonObject = nlohmann::json::parse(jsonStr);

    NetworkTransferData networkTransferData(jsonObject.value("type", ""), jsonObject.value("id", ""));
    networkTransferData.setPassword(jsonObject.value("password", ""));
    networkTransferData.setDbName(jsonObject.value("dbName", ""));
    networkTransferData.setSql(jsonObject.value("sql", ""));
    networkTransferData.setSuccess(jsonObject.value("success", false));
    networkTransferData.setMessage(jsonObject.value("message", ""));
    networkTransferData.setAffectedRows(jsonObject.value("affectedRows", 0));
    networkTransferData.setColumns(jsonObject.value("columns", std::vector<std::string> {}));
    networkTransferData.setRows(jsonObject.value("rows", std::vector<std::vector<std::string>> {}));

    std::vector<DatabaseNode> databaseNodes;
    if (jsonObject.contains("databases") && jsonObject["databases"].is_array()) {
        for (const nlohmann::json &databaseJson : jsonObject["databases"]) {
            databaseNodes.push_back(DatabaseNode::fromJson(databaseJson.dump()));
        }
    }
    networkTransferData.setDatabases(databaseNodes);
    return networkTransferData;
}

const std::string &NetworkTransferData::getType() const
{
    return type;
}

void NetworkTransferData::setType(const std::string &type)
{
    this->type = type;
}

const std::string &NetworkTransferData::getId() const
{
    return id;
}

void NetworkTransferData::setId(const std::string &id)
{
    this->id = id;
}

const std::string &NetworkTransferData::getPassword() const
{
    return password;
}

void NetworkTransferData::setPassword(const std::string &password)
{
    this->password = password;
}

const std::string &NetworkTransferData::getDbName() const
{
    return dbName;
}

void NetworkTransferData::setDbName(const std::string &dbName)
{
    this->dbName = dbName;
}

const std::string &NetworkTransferData::getSql() const
{
    return sql;
}

void NetworkTransferData::setSql(const std::string &sql)
{
    this->sql = sql;
}

bool NetworkTransferData::getSuccess() const
{
    return success;
}

void NetworkTransferData::setSuccess(bool success)
{
    this->success = success;
}

const std::string &NetworkTransferData::getMessage() const
{
    return message;
}

void NetworkTransferData::setMessage(const std::string &message)
{
    this->message = message;
}

int NetworkTransferData::getAffectedRows() const
{
    return affectedRows;
}

void NetworkTransferData::setAffectedRows(int affectedRows)
{
    this->affectedRows = affectedRows;
}

const std::vector<std::string> &NetworkTransferData::getColumns() const
{
    return columns;
}

void NetworkTransferData::setColumns(const std::vector<std::string> &columns)
{
    this->columns = columns;
}

const std::vector<std::vector<std::string>> &NetworkTransferData::getRows() const
{
    return rows;
}

void NetworkTransferData::setRows(const std::vector<std::vector<std::string>> &rows)
{
    this->rows = rows;
}

const std::vector<DatabaseNode> &NetworkTransferData::getDatabases() const
{
    return databases;
}

void NetworkTransferData::setDatabases(const std::vector<DatabaseNode> &databases)
{
    this->databases = databases;
}
