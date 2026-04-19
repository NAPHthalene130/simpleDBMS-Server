#include "NetData.h"

#include <nlohmann/json.hpp>

NetData::NetData() = default;

NetData::NetData(const std::string &type, const std::string &content)
    : type(type), content(content)
{
}

std::string NetData::toJson() const
{
    nlohmann::json jsonObject;
    jsonObject["type"] = type;
    jsonObject["content"] = content;
    return jsonObject.dump();
}

NetData NetData::fromJson(const std::string &jsonStr)
{
    const nlohmann::json jsonObject = nlohmann::json::parse(jsonStr);
    return NetData(jsonObject.value("type", ""), jsonObject.value("content", ""));
}

const std::string &NetData::getType() const
{
    return type;
}

void NetData::setType(const std::string &type)
{
    this->type = type;
}

const std::string &NetData::getContent() const
{
    return content;
}

void NetData::setContent(const std::string &content)
{
    this->content = content;
}
